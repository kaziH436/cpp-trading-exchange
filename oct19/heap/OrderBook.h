#pragma once

// VERSION 2: heap only.
// Each side is a binary heap (std::vector managed with the std heap functions) with one
// entry per price: {price, head, tail} where head/tail are the pool indices of the oldest and
// newest order in that price's doubly linked list. Buys use a max heap, sells a min heap.
//   best price / best order : O(1)
//   remove an emptied best price : O(log L)
//   order at a new price : O(L) scan to check + O(log L) push
//   order at an existing price : O(L) scan + O(1) append
//   cancel that empties a price : O(L) scan + O(L) rebuild (make_heap)
// L = number of distinct prices on that side.

#include "./oct12/mem_pool.h"
#include "order.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class OrderBookHeap {
public:
    enum class SubmitStatus {
        RestingInBook,
        FilledCompletely,
        IgnoredWrongTicker,
        PoolFull,
        MarketRemainderHeld
    };

    struct SubmitResult {
        SubmitStatus status;
        OrderId orderId;
        Quantity remaining;
    };

    // One OrderBookHeap manages exactly one ticker.
    // poolCapacity is fixed for the lifetime of this book.
    explicit OrderBookHeap(std::string_view ticker, std::size_t poolCapacity)
        : ticker_(validatedTicker(ticker)), pool_(poolCapacity) {
        orderIdToIndex_.reserve(poolCapacity);
        heldMarketOrderIndices_.reserve(poolCapacity);
        // Every price holds at least one pooled order, so a side never has more
        // prices than the pool has slots. Reserving once means the heaps never reallocate.
        buys_.reserve(poolCapacity);
        sells_.reserve(poolCapacity);
    }

    OrderBookHeap() = delete;
    OrderBookHeap(const OrderBookHeap&) = delete;
    OrderBookHeap& operator=(const OrderBookHeap&) = delete;

    const std::string& ticker() const noexcept { return ticker_; }

    std::size_t restingOrderCount() const noexcept {
        return orderIdToIndex_.size();
    }

    std::size_t heldMarketRemainderCount() const noexcept {
        return heldMarketOrderIndices_.size();
    }

    // Best resting price on each side, or nullopt when that side is empty.
    std::optional<Price> bestBid() const {
        return buys_.empty() ? std::nullopt : std::optional<Price>(buys_.front().price);
    }
    std::optional<Price> bestAsk() const {
        return sells_.empty() ? std::nullopt : std::optional<Price>(sells_.front().price);
    }
    // Number of distinct price levels on one side.
    std::size_t priceLevelCount(Side side) const noexcept {
        return side == Side::Buy ? buys_.size() : sells_.size();
    }

    // Main entry point for an incoming order. This invokes matching first, then
    // rests any unfilled limit-order quantity. Wrong-ticker orders are ignored.
    SubmitResult submitOrder(Order incoming) {
        const OrderId id = incoming.orderId;
        const Quantity submittedRemaining = incoming.remaining;

        if (incoming.tickerView() != std::string_view(ticker_)) {
            return {SubmitStatus::IgnoredWrongTicker, id, submittedRemaining};
        }

        // The caller is responsible for ensuring order IDs are globally unique.

        // New submitted orders should not already be linked into another queue.
        incoming.prev = kNullIndex;
        incoming.next = kNullIndex;

        const auto incomingIndex = pool_.allocate(std::move(incoming));
        if (incomingIndex == MemPool<Order>::kInvalidIndex) {
            return {SubmitStatus::PoolFull, id, submittedRemaining};
        }

        match(incomingIndex);

        Order& processed = pool_.get(incomingIndex);
        if (processed.remaining == 0) {
            const Quantity remaining = processed.remaining;
            pool_.deallocate(incomingIndex);
            return {SubmitStatus::FilledCompletely, id, remaining};
        }

        if (processed.type == OrderType::Limit) {
            const Quantity remaining = processed.remaining;
            rest(incomingIndex);
            return {SubmitStatus::RestingInBook, id, remaining};
        }

        // ON HOLD, as requested: keep the unfilled market order outside the book.
        const Quantity remaining = processed.remaining;
        heldMarketOrderIndices_.push_back(incomingIndex);
        return {SubmitStatus::MarketRemainderHeld, id, remaining};
    }

    // Returns true if a resting order was canceled; false if its ID is not active
    // in the book (including orders that have already completed).
    bool cancel(OrderId orderId) {
        const auto idIt = orderIdToIndex_.find(orderId);
        if (idIt == orderIdToIndex_.end()) {
            return false;
        }

        const std::uint32_t index = idIt->second;
        const Order& order = pool_.get(index);
        const Side side = order.side;
        const Price price = order.price;

        if (side == Side::Buy) {
            unlinkOrder(buys_, BuyCompare{}, price, index);
        } else {
            unlinkOrder(sells_, SellCompare{}, price, index);
        }

        orderIdToIndex_.erase(idIt);
        pool_.deallocate(index);
        return true;
    }

private:
    // One heap entry per price. price is the first member so the defaulted <=>
    // orders entries by price (prices are unique within a heap).
    struct PriceLevel {
        Price price = 0;
        std::uint32_t head = kNullIndex;
        std::uint32_t tail = kNullIndex;
        auto operator<=>(const PriceLevel&) const = default;
    };

    using LevelHeap = std::vector<PriceLevel>;
    using BuyCompare = std::less<PriceLevel>;      // max heap: highest bid on top
    using SellCompare = std::greater<PriceLevel>;  // min heap: lowest ask on top

    static std::string validatedTicker(std::string_view ticker) {
        if (ticker.empty() || ticker.size() >= Order::kTickerCapacity) {
            throw std::invalid_argument(
                "OrderBook ticker must contain between 1 and " +
                std::to_string(Order::kTickerCapacity - 1) + " characters");
        }
        return std::string(ticker);
    }

    static bool priceAllowsTrade(const Order& incoming, Price bestOppositePrice) {
        if (incoming.type == OrderType::Market) {
            return true;
        }
        if (incoming.side == Side::Buy) {
            return bestOppositePrice <= incoming.price;
        }
        return bestOppositePrice >= incoming.price;
    }

    static LevelHeap::iterator findLevel(LevelHeap& heap, Price price) {
        return std::find_if(heap.begin(), heap.end(),
                            [price](const PriceLevel& level) { return level.price == price; });
    }

    void match(std::uint32_t incomingIndex) {
        while (pool_.get(incomingIndex).remaining > 0) {
            const Side incomingSide = pool_.get(incomingIndex).side;
            const bool traded = (incomingSide == Side::Buy)
                                    ? matchOne(incomingIndex, sells_, SellCompare{})
                                    : matchOne(incomingIndex, buys_, BuyCompare{});
            if (!traded) {
                break;
            }
        }
    }

    // Attempts one trade against the best price (top of the opposite heap).
    // Returns false if that side is empty or the best price is not eligible.
    template <typename Compare>
    bool matchOne(std::uint32_t incomingIndex, LevelHeap& opposite, Compare comp) {
        if (opposite.empty()) {
            return false;
        }

        PriceLevel& best = opposite.front();
        Order& incoming = pool_.get(incomingIndex);
        if (!priceAllowsTrade(incoming, best.price)) {
            return false;
        }

        const std::uint32_t restingIndex = best.head;
        if (restingIndex == kNullIndex) {
            throw std::logic_error("Price level has no head order");
        }

        Order& resting = pool_.get(restingIndex);
        const Quantity tradedQuantity = std::min(incoming.remaining, resting.remaining);
        if (tradedQuantity == 0) {
            throw std::logic_error("Cannot match an order with zero remaining quantity");
        }

        incoming.fill(tradedQuantity);
        resting.fill(tradedQuantity);

        if (resting.isFilled()) {
            removeFilledHead(opposite, comp, restingIndex);
        }
        // If the resting order is only partially filled, it remains at the head.
        return true;
    }

    template <typename Compare>
    void removeFilledHead(LevelHeap& opposite, Compare comp, std::uint32_t restingIndex) {
        Order& resting = pool_.get(restingIndex);
        const OrderId completedOrderId = resting.orderId;
        const std::uint32_t nextIndex = resting.next;

        PriceLevel& level = opposite.front();
        level.head = nextIndex;
        if (nextIndex != kNullIndex) {
            pool_.get(nextIndex).prev = kNullIndex;
        } else {
            level.tail = kNullIndex;
        }

        if (level.head == kNullIndex) {
            // The best price is empty: pop it off the top of the heap, O(log L).
            std::pop_heap(opposite.begin(), opposite.end(), comp);
            opposite.pop_back();
        }

        orderIdToIndex_.erase(completedOrderId);
        pool_.deallocate(restingIndex);
    }

    void rest(std::uint32_t orderIndex) {
        const Order& order = pool_.get(orderIndex);
        const OrderId orderId = order.orderId;

        const auto [idIt, inserted] = orderIdToIndex_.emplace(orderId, orderIndex);
        if (!inserted) {
            throw std::logic_error("Order ID is already present in the active-order map");
        }

        try {
            if (order.side == Side::Buy) {
                appendToPriceLevel(buys_, BuyCompare{}, orderIndex, order.price);
            } else {
                appendToPriceLevel(sells_, SellCompare{}, orderIndex, order.price);
            }
        } catch (...) {
            orderIdToIndex_.erase(idIt);
            throw;
        }
    }

    template <typename Compare>
    void appendToPriceLevel(LevelHeap& heap, Compare comp, std::uint32_t orderIndex, Price price) {
        Order& order = pool_.get(orderIndex);
        order.next = kNullIndex;

        const auto levelIt = findLevel(heap, price);  // O(L): a heap cannot search by price
        if (levelIt == heap.end()) {
            // New price: push a new entry and sift it up, O(log L).
            order.prev = kNullIndex;
            heap.push_back(PriceLevel{price, orderIndex, orderIndex});
            std::push_heap(heap.begin(), heap.end(), comp);
            return;
        }

        // Existing price: append to the tail of its list, O(1).
        order.prev = levelIt->tail;
        pool_.get(levelIt->tail).next = orderIndex;
        levelIt->tail = orderIndex;
    }

    template <typename Compare>
    void unlinkOrder(LevelHeap& heap, Compare comp, Price price, std::uint32_t orderIndex) {
        const auto levelIt = findLevel(heap, price);
        if (levelIt == heap.end()) {
            throw std::logic_error("Active order refers to a missing price level");
        }

        Order& order = pool_.get(orderIndex);
        PriceLevel& level = *levelIt;
        const std::uint32_t previousIndex = order.prev;
        const std::uint32_t nextIndex = order.next;

        if (previousIndex != kNullIndex) {
            pool_.get(previousIndex).next = nextIndex;
        } else {
            level.head = nextIndex;
        }

        if (nextIndex != kNullIndex) {
            pool_.get(nextIndex).prev = previousIndex;
        } else {
            level.tail = previousIndex;
        }

        if (level.head == kNullIndex) {
            // The price is empty and may sit in the middle of the heap: remove it,
            // then rebuild the heap, O(L). Cancels are rare, so this is acceptable.
            *levelIt = heap.back();
            heap.pop_back();
            std::make_heap(heap.begin(), heap.end(), comp);
        }
    }

    std::string ticker_;
    MemPool<Order> pool_;
    LevelHeap buys_;
    LevelHeap sells_;
    std::unordered_map<OrderId, std::uint32_t> orderIdToIndex_;
    std::vector<std::uint32_t> heldMarketOrderIndices_;
};
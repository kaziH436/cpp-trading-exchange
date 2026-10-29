#pragma once

// VERSION 3: heap of prices + hash table from price to level.
// Each side keeps (a) a binary heap of the distinct prices (std::vector managed with the std
// heap functions; max heap for buys, min heap for sells) and (b) an unordered_map from price to
// the head/tail pool indices of that price's doubly linked list of orders.
//   best price / best order : O(1)  (heap top, then one hash lookup)
//   remove an emptied best price : O(log L)
//   order at an existing price : O(1) hash lookup + O(1) append
//   order at a new price : O(1) check + O(log L) push
//   cancel that empties a price : O(L) find in the heap + O(L) rebuild (make_heap)
// L = number of distinct prices on that side.

#include "../../oct12/mem_pool.h"
#include "../order.h"

#include <algorithm>
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

class OrderBookHeapHash {
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

    // One OrderBookHeapHash manages exactly one ticker.
    // poolCapacity is fixed for the lifetime of this book.
    explicit OrderBookHeapHash(std::string_view ticker, std::size_t poolCapacity)
        : ticker_(validatedTicker(ticker)), pool_(poolCapacity) {
        orderIdToIndex_.reserve(poolCapacity);
        heldMarketOrderIndices_.reserve(poolCapacity);
        // Every price holds at least one pooled order, so a side never has more
        // prices than the pool has slots. Reserving once means nothing reallocates or rehashes.
        buyPrices_.reserve(poolCapacity);
        sellPrices_.reserve(poolCapacity);
        buyLevels_.reserve(poolCapacity);
        sellLevels_.reserve(poolCapacity);
    }

    OrderBookHeapHash() = delete;
    OrderBookHeapHash(const OrderBookHeapHash&) = delete;
    OrderBookHeapHash& operator=(const OrderBookHeapHash&) = delete;

    const std::string& ticker() const noexcept { return ticker_; }

    std::size_t restingOrderCount() const noexcept {
        return orderIdToIndex_.size();
    }

    std::size_t heldMarketRemainderCount() const noexcept {
        return heldMarketOrderIndices_.size();
    }

    // Best resting price on each side, or nullopt when that side is empty.
    std::optional<Price> bestBid() const {
        return buyPrices_.empty() ? std::nullopt : std::optional<Price>(buyPrices_.front());
    }
    std::optional<Price> bestAsk() const {
        return sellPrices_.empty() ? std::nullopt : std::optional<Price>(sellPrices_.front());
    }
    // Number of distinct price levels on one side.
    std::size_t priceLevelCount(Side side) const noexcept {
        return side == Side::Buy ? buyPrices_.size() : sellPrices_.size();
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
            unlinkOrder(buyPrices_, buyLevels_, BuyCompare{}, price, index);
        } else {
            unlinkOrder(sellPrices_, sellLevels_, SellCompare{}, price, index);
        }

        orderIdToIndex_.erase(idIt);
        pool_.deallocate(index);
        return true;
    }

private:
    struct PriceLevel {
        std::uint32_t head = kNullIndex;
        std::uint32_t tail = kNullIndex;
    };

    using PriceHeap = std::vector<Price>;
    using LevelMap = std::unordered_map<Price, PriceLevel>;
    using BuyCompare = std::less<Price>;      // max heap: highest bid on top
    using SellCompare = std::greater<Price>;  // min heap: lowest ask on top

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

    void match(std::uint32_t incomingIndex) {
        while (pool_.get(incomingIndex).remaining > 0) {
            const Side incomingSide = pool_.get(incomingIndex).side;
            const bool traded = (incomingSide == Side::Buy)
                                    ? matchOne(incomingIndex, sellPrices_, sellLevels_, SellCompare{})
                                    : matchOne(incomingIndex, buyPrices_, buyLevels_, BuyCompare{});
            if (!traded) {
                break;
            }
        }
    }

    // Attempts one trade against the best price (top of the opposite heap).
    // Returns false if that side is empty or the best price is not eligible.
    template <typename Compare>
    bool matchOne(std::uint32_t incomingIndex, PriceHeap& prices, LevelMap& levels, Compare comp) {
        if (prices.empty()) {
            return false;
        }

        const Price bestPrice = prices.front();
        Order& incoming = pool_.get(incomingIndex);
        if (!priceAllowsTrade(incoming, bestPrice)) {
            return false;
        }

        const auto levelIt = levels.find(bestPrice);
        if (levelIt == levels.end()) {
            throw std::logic_error("Price heap entry has no level");
        }

        const std::uint32_t restingIndex = levelIt->second.head;
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
            removeFilledHead(prices, levels, comp, levelIt, restingIndex);
        }
        // If the resting order is only partially filled, it remains at the head.
        return true;
    }

    template <typename Compare>
    void removeFilledHead(PriceHeap& prices,
                          LevelMap& levels,
                          Compare comp,
                          typename LevelMap::iterator levelIt,
                          std::uint32_t restingIndex) {
        Order& resting = pool_.get(restingIndex);
        const OrderId completedOrderId = resting.orderId;
        const std::uint32_t nextIndex = resting.next;

        PriceLevel& level = levelIt->second;
        level.head = nextIndex;
        if (nextIndex != kNullIndex) {
            pool_.get(nextIndex).prev = kNullIndex;
        } else {
            level.tail = kNullIndex;
        }

        if (level.head == kNullIndex) {
            // The best price is empty: drop its level and pop it off the heap, O(log L).
            levels.erase(levelIt);
            std::pop_heap(prices.begin(), prices.end(), comp);
            prices.pop_back();
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
                appendToPriceLevel(buyPrices_, buyLevels_, BuyCompare{}, orderIndex, order.price);
            } else {
                appendToPriceLevel(sellPrices_, sellLevels_, SellCompare{}, orderIndex, order.price);
            }
        } catch (...) {
            orderIdToIndex_.erase(idIt);
            throw;
        }
    }

    template <typename Compare>
    void appendToPriceLevel(PriceHeap& prices,
                            LevelMap& levels,
                            Compare comp,
                            std::uint32_t orderIndex,
                            Price price) {
        Order& order = pool_.get(orderIndex);
        order.next = kNullIndex;

        auto [levelIt, created] = levels.try_emplace(price, PriceLevel{});  // O(1) lookup
        PriceLevel& level = levelIt->second;

        if (created) {
            // New price: first order in the level, and a new entry sifted into the heap, O(log L).
            order.prev = kNullIndex;
            level.head = orderIndex;
            level.tail = orderIndex;
            prices.push_back(price);
            std::push_heap(prices.begin(), prices.end(), comp);
            return;
        }

        // Existing price: append to the tail of its list, O(1).
        order.prev = level.tail;
        pool_.get(level.tail).next = orderIndex;
        level.tail = orderIndex;
    }

    template <typename Compare>
    void unlinkOrder(PriceHeap& prices,
                     LevelMap& levels,
                     Compare comp,
                     Price price,
                     std::uint32_t orderIndex) {
        const auto levelIt = levels.find(price);
        if (levelIt == levels.end()) {
            throw std::logic_error("Active order refers to a missing price level");
        }

        Order& order = pool_.get(orderIndex);
        PriceLevel& level = levelIt->second;
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
            // The price is empty and may sit in the middle of the heap: drop its level,
            // remove it from the heap, then rebuild the heap, O(L). Cancels are rare.
            levels.erase(levelIt);
            const auto priceIt = std::find(prices.begin(), prices.end(), price);
            if (priceIt == prices.end()) {
                throw std::logic_error("Price level is missing from the price heap");
            }
            *priceIt = prices.back();
            prices.pop_back();
            std::make_heap(prices.begin(), prices.end(), comp);
        }
    }

    std::string ticker_;
    MemPool<Order> pool_;
    PriceHeap buyPrices_;
    PriceHeap sellPrices_;
    LevelMap buyLevels_;
    LevelMap sellLevels_;
    std::unordered_map<OrderId, std::uint32_t> orderIdToIndex_;
    std::vector<std::uint32_t> heldMarketOrderIndices_;
};
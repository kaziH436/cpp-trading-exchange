#pragma once

#include "./oct12/mem_pool.h"
#include "order.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map> // convert to priority queue
#include <utility>
#include <vector>

class OrderBook {
public:
    enum class SubmitStatus {
        RestingInBook,
        FilledCompletely,
        IgnoredWrongTicker,
        DuplicateActiveOrderId,
        PoolFull,
        MarketRemainderHeld
    };

    struct SubmitResult {
        SubmitStatus status;
        OrderId orderId;
        Quantity remaining;
    };

    // One OrderBook manages exactly one ticker.
    // poolCapacity is fixed for the lifetime of this book.
    explicit OrderBook(std::string_view ticker, std::size_t poolCapacity)
        : ticker_(validatedTicker(ticker)), pool_(poolCapacity) {
        orderIdToIndex_.reserve(poolCapacity);
        heldMarketOrderIndices_.reserve(poolCapacity);
    };

    OrderBook() = delete;
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    const std::string& ticker() const noexcept { return ticker_; }

    std::size_t restingOrderCount() const noexcept {
        return orderIdToIndex_.size();
    }

    // These orders are intentionally kept outside the book while the market-order
    // remainder policy remains unresolved. They are not resting orders and cannot
    // be canceled through cancel(), whose lookup contains resting orders only.
    std::size_t heldMarketRemainderCount() const noexcept {
        return heldMarketOrderIndices_.size();
    }

    // Main entry point for an incoming order. This invokes matching first, then
    // rests any unfilled limit-order quantity. Wrong-ticker orders are ignored.
    SubmitResult submitOrder(Order incoming) {
        const OrderId id = incoming.orderId;
        const Quantity submittedRemaining = incoming.remaining;

        if (incoming.tickerView() != std::string_view(ticker_)) {
            return {SubmitStatus::IgnoredWrongTicker, id, submittedRemaining};
        }

        // Only active resting IDs are kept in this map. The caller is responsible
        // for ensuring IDs are globally unique, including after orders complete.
        if (orderIdToIndex_.contains(id)) {
            return {SubmitStatus::DuplicateActiveOrderId, id, submittedRemaining};
        }

        // New submitted orders should not already be linked into another queue.
        incoming.prev = kNullIndex;
        incoming.next = kNullIndex;

        // Keep the incoming order in the pool while it is being matched, as in
        // the supplied pseudocode. Capacity must allow a slot for this order.
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
        // It is not added to orderIdToIndex_ and will not be re-matched automatically.
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
            unlinkOrder(buys_, price, index);
        } else {
            unlinkOrder(sells_, price, index);
        }

        orderIdToIndex_.erase(idIt);
        pool_.deallocate(index);
        return true;
    }

    // Prints both sides best-price-first, with FIFO order within each price level.
    // Prices are displayed in the integer units used by Order::price.
    std::string toString() const {
        std::ostringstream out;
        out << "OrderBook: " << ticker_ << '\n';
        out << "BUYS (highest price first)\n";
        writeSide(out, buys_);
        out << "SELLS (lowest price first)\n";
        writeSide(out, sells_);
        return out.str();
    }

private:
    struct PriceLevel {
        std::uint32_t head = kNullIndex;
        std::uint32_t tail = kNullIndex;
    };

    using BuyMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using SellMap = std::map<Price, PriceLevel, std::less<Price>>;

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
                                    ? matchOne(incomingIndex, sells_)
                                    : matchOne(incomingIndex, buys_);
            if (!traded) {
                break;
            }
        }
    }

    // Attempts one trade against the best price level on the opposite side.
    // Returns false if the book is empty on that side or the best price is not eligible.
    template <typename PriceMap>
    bool matchOne(std::uint32_t incomingIndex, PriceMap& opposite) {
        if (opposite.empty()) {
            return false;
        }

        auto bestLevel = opposite.begin();
        Order& incoming = pool_.get(incomingIndex);
        if (!priceAllowsTrade(incoming, bestLevel->first)) {
            return false;
        }

        const std::uint32_t restingIndex = bestLevel->second.head;
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
            removeFilledHead(opposite, bestLevel, restingIndex);
        }
        // If the resting order is only partially filled, it remains at the head.
        return true;
    }

    template <typename PriceMap>
    void removeFilledHead(PriceMap& opposite,
                          typename PriceMap::iterator levelIt,
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
            opposite.erase(levelIt);
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
                appendToPriceLevel(buys_, orderIndex, order.price);
            } else {
                appendToPriceLevel(sells_, orderIndex, order.price);
            }
        } catch (...) {
            orderIdToIndex_.erase(idIt);
            throw;
        }
    }

    template <typename PriceMap>
    void appendToPriceLevel(PriceMap& sideMap,
                            std::uint32_t orderIndex,
                            Price price) {
        auto [levelIt, created] = sideMap.try_emplace(price, PriceLevel{});
        (void)created;

        Order& order = pool_.get(orderIndex);
        PriceLevel& level = levelIt->second;
        order.next = kNullIndex;
        order.prev = level.tail;

        if (level.tail == kNullIndex) {
            // First order at this price level.
            level.head = orderIndex;
        } else {
            // Connect the previous tail to the new order.
            pool_.get(level.tail).next = orderIndex;
        }

        level.tail = orderIndex;
    }

    template <typename PriceMap>
    void unlinkOrder(PriceMap& sideMap, Price price, std::uint32_t orderIndex) {
        auto levelIt = sideMap.find(price);
        if (levelIt == sideMap.end()) {
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
            sideMap.erase(levelIt);
        }
    }

    template <typename PriceMap>
    void writeSide(std::ostringstream& out, const PriceMap& sideMap) const {
        if (sideMap.empty()) {
            out << "  (empty)\n";
            return;
        }

        for (const auto& [price, level] : sideMap) {
            out << "  Price " << price << ":\n";

            std::uint32_t currentIndex = level.head;
            while (currentIndex != kNullIndex) {
                const Order& order = pool_.get(currentIndex);
                out << "    " << order.toString() << '\n';
                currentIndex = order.next;
            }
        }
    }

    std::string ticker_;
    MemPool<Order> pool_;
    BuyMap buys_;
    SellMap sells_;
    std::unordered_map<OrderId, std::uint32_t> orderIdToIndex_;
    std::vector<std::uint32_t> heldMarketOrderIndices_;
};

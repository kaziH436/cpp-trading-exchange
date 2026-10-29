#pragma once

// heap only.
// Each side is a binary heap (std::vector managed with the std heap functions) with one
// entry per price: {price, head, tail} where head/tail are the pool indices of the oldest and
// newest order in that price's doubly linked list. Buys use a max heap, sells a min heap.
//   best price / best order : O(1)
//   remove an emptied best price : O(log L)
//   order at a new price : O(L) scan to check + O(log L) push
//   order at an existing price : O(L) scan + O(1) append
//   cancel that empties a price : O(L) scan + O(L) rebuild (make_heap)
// L = number of distinct prices on that side.
//
// Features added on top of the basic book:
//   update(orderId, newQuantity)  down-size keeps the queue spot, up-size goes to the back
//   disconnect(clientId)          cancel every order (resting and held) a client owns
//   self-trade prevention         decrement-and-cancel, see matchOne()

#include "../../oct12/mem_pool.h"
#include "../order.h"

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
        MarketRemainderHeld,
        // Self-trade prevention removed some or all of this incoming order and nothing
        // is left of it (it neither rests nor is held).
        CanceledBySelfTrade
    };

    struct SubmitResult {
        SubmitStatus status;
        OrderId orderId;
        Quantity remaining;
    };

    enum class UpdateStatus {
        Updated,          // includes "same quantity", which changes nothing
        NotFound,         // not a resting order (unknown, filled, canceled, or a held market order)
        InvalidQuantity   // a new quantity of 0 is rejected; use cancel() instead
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

        const Quantity selfTradeCanceled = match(incomingIndex);

        Order& processed = pool_.get(incomingIndex);
        if (processed.remaining == 0) {
            const Quantity remaining = processed.remaining;
            pool_.deallocate(incomingIndex);
            // If self-trade prevention removed any part of this order, report that.
            // Otherwise every share traded, so it was filled completely.
            return {selfTradeCanceled > 0 ? SubmitStatus::CanceledBySelfTrade
                                          : SubmitStatus::FilledCompletely,
                    id, remaining};
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

    // Changes the REMAINING quantity of a resting order to newQuantity.
    //
    // Example queue at price 100, oldest first: A(10) B(5) C(8)
    //   update(A, 6)  down-size: A keeps the front of the line, nothing moves.
    //   update(B, 9)  up-size:   B loses its spot and goes to the back: A C B.
    // Why: if up-sizing kept the spot, someone could rest a tiny order early, hold the
    // front of the line, then inflate it and jump everyone who waited. A down-size gives
    // up nothing, so it keeps its place.
    //
    // "Back of the line" means unlink the order and append it at the tail of its queue.
    // It does NOT mean swapping with the last order: with A B C D, up-sizing B must give
    // A C D B, but a swap would give A D C B and D would jump ahead of C.
    //
    // Same quantity: nothing changes, the order keeps its place (a no-op).
    // Cost: O(1) to change the order, plus O(L) to find the price's heap entry on an
    // up-size (a heap cannot search by price). A down-size never touches the heap.
    UpdateStatus update(OrderId orderId, Quantity newQuantity) {
        if (newQuantity == 0) {
            return UpdateStatus::InvalidQuantity;
        }

        const auto idIt = orderIdToIndex_.find(orderId);
        if (idIt == orderIdToIndex_.end()) {
            return UpdateStatus::NotFound;
        }

        const std::uint32_t index = idIt->second;
        Order& order = pool_.get(index);
        if (newQuantity == order.remaining) {
            return UpdateStatus::Updated;
        }

        const bool upsize = newQuantity > order.remaining;
        order.setRemaining(newQuantity);  // throws before changing anything if it overflows
        if (upsize) {
            if (order.side == Side::Buy) {
                moveToBackOfQueue(buys_, order.price, index);
            } else {
                moveToBackOfQueue(sells_, order.price, index);
            }
        }
        return UpdateStatus::Updated;
    }

    // Cancels every order the client owns: resting orders and held market remainders.
    // Returns how many orders were removed. It does not block the client from
    // submitting new orders afterwards (no connection state is kept).
    //
    // Resting orders are found by walking every price's queue, O(n) in total, instead of
    // scanning the order-ID table. Reason: in this heap-only book each order found via the
    // ID table would still need an O(L) search for its price entry, which makes the whole
    // thing O(n + k*L) for k orders. Walking the queues already holds the entry.
    // Held market orders are NOT in any queue and are never freed by matching or cancel(),
    // so they get their own pass here to give their pool slots back.
    std::size_t disconnect(ClientId clientId) {
        std::size_t removed = removeClientOrders(buys_, BuyCompare{}, clientId);
        removed += removeClientOrders(sells_, SellCompare{}, clientId);

        std::size_t kept = 0;
        for (const std::uint32_t index : heldMarketOrderIndices_) {
            if (pool_.get(index).clientId == clientId) {
                pool_.deallocate(index);
                ++removed;
            } else {
                heldMarketOrderIndices_[kept++] = index;
            }
        }
        heldMarketOrderIndices_.resize(kept);
        return removed;
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

    // Matches the incoming order against the book. Returns how many shares of the
    // incoming order were removed by self-trade prevention (0 if none).
    Quantity match(std::uint32_t incomingIndex) {
        Quantity selfTradeCanceled = 0;
        while (pool_.get(incomingIndex).remaining > 0) {
            const Side incomingSide = pool_.get(incomingIndex).side;
            const bool traded =
                (incomingSide == Side::Buy)
                    ? matchOne(incomingIndex, sells_, SellCompare{}, selfTradeCanceled)
                    : matchOne(incomingIndex, buys_, BuyCompare{}, selfTradeCanceled);
            if (!traded) {
                break;
            }
        }
        return selfTradeCanceled;
    }

    // Attempts one trade against the best price (top of the opposite heap).
    // Returns false if that side is empty or the best price is not eligible.
    template <typename Compare>
    bool matchOne(std::uint32_t incomingIndex, LevelHeap& opposite, Compare comp,
                  Quantity& selfTradeCanceled) {
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

        // SELF-TRADE PREVENTION, decrement-and-cancel. The two orders belong to the same
        // client, so they must never trade with each other. Instead the OVERLAP, the
        // smaller of the two quantities, is removed from BOTH orders. No trade happens.
        //
        // Example: client 1 has a resting sell of 10 and sends a buy of 4 at a crossing price.
        //   overlap = 4 -> the buy has 0 left and is gone, the sell keeps 6 in its same spot.
        // Example: client 1 has a resting sell of 4 and sends a buy of 10.
        //   overlap = 4 -> the sell is gone, the buy keeps 6 and carries on matching the NEXT
        //   order in line (which may be another client's, and may even be the client's own
        //   again, in which case this rule runs again).
        //
        // Price-time priority is kept: the incoming order meets the queue strictly front to
        // back. It never skips its own order to reach someone behind it, and it never looks
        // past the best price while a better-priority order is still there. Because the
        // overlap is removed, no crossed book (bid >= ask) is ever left resting.
        //
        // This only runs where a trade WOULD have happened (the price check above passed).
        // Removing shares this way is not a fill: setRemaining() keeps filled() honest.
        if (resting.clientId == incoming.clientId) {
            const Quantity overlap = std::min(incoming.remaining, resting.remaining);
            if (overlap == 0) {
                throw std::logic_error("Cannot match an order with zero remaining quantity");
            }
            incoming.setRemaining(incoming.remaining - overlap);
            resting.setRemaining(resting.remaining - overlap);
            selfTradeCanceled += overlap;
            if (resting.remaining == 0) {
                // Same removal as a fully filled head: pop it from its queue (and the heap
                // if its price is now empty), erase its ID, free its pool slot.
                removeFilledHead(opposite, comp, restingIndex);
            }
            return true;
        }

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

    // Pointer surgery only: takes the order out of its price's doubly linked list and fixes
    // the level's head/tail. If the list becomes empty the heap entry is left in place.
    void unlinkFromQueue(PriceLevel& level, Order& order) {
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
    }

    // Used by update() on an up-size: moves the order to the tail of its price's queue.
    void moveToBackOfQueue(LevelHeap& heap, Price price, std::uint32_t orderIndex) {
        Order& order = pool_.get(orderIndex);
        if (order.next == kNullIndex) {
            return;  // already last in line (this includes being the only order at its price)
        }

        const auto levelIt = findLevel(heap, price);  // O(L): a heap cannot search by price
        if (levelIt == heap.end()) {
            throw std::logic_error("Active order refers to a missing price level");
        }

        PriceLevel& level = *levelIt;
        unlinkFromQueue(level, order);  // not the tail, so level.tail is unchanged
        order.prev = level.tail;
        order.next = kNullIndex;
        pool_.get(level.tail).next = orderIndex;
        level.tail = orderIndex;
    }

    // Used by disconnect(): removes every order of one client from one side of the book.
    // Walks each price's queue (grabbing the next link before removing the current order),
    // then drops the prices that ended up empty and rebuilds the heap ONCE, O(L), instead
    // of once per emptied price.
    template <typename Compare>
    std::size_t removeClientOrders(LevelHeap& heap, Compare comp, ClientId clientId) {
        std::size_t removed = 0;
        for (PriceLevel& level : heap) {
            std::uint32_t index = level.head;
            while (index != kNullIndex) {
                Order& order = pool_.get(index);
                const std::uint32_t nextIndex = order.next;
                if (order.clientId == clientId) {
                    unlinkFromQueue(level, order);
                    orderIdToIndex_.erase(order.orderId);
                    pool_.deallocate(index);
                    ++removed;
                }
                index = nextIndex;
            }
        }

        const auto firstEmpty = std::remove_if(
            heap.begin(), heap.end(),
            [](const PriceLevel& level) { return level.head == kNullIndex; });
        if (firstEmpty != heap.end()) {
            heap.erase(firstEmpty, heap.end());
            std::make_heap(heap.begin(), heap.end(), comp);
        }
        return removed;
    }

    template <typename Compare>
    void unlinkOrder(LevelHeap& heap, Compare comp, Price price, std::uint32_t orderIndex) {
        const auto levelIt = findLevel(heap, price);
        if (levelIt == heap.end()) {
            throw std::logic_error("Active order refers to a missing price level");
        }

        PriceLevel& level = *levelIt;
        unlinkFromQueue(level, pool_.get(orderIndex));

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
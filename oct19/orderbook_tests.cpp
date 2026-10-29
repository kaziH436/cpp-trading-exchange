// Google Test suite for the heap-only order book (OrderBookHeap in OrderBook.h).
//
// Sections:
//   1. Core behavior (matching, price priority, FIFO, market orders, pool, cancel)
//   2. update(orderId, newQuantity)
//   3. disconnect(clientId)
//   4. Self-trade prevention (decrement-and-cancel)
//   5. Order::setRemaining
//   6. Randomized comparison against a deliberately simple reference book

#include "OrderBook.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Status = OrderBookHeap::SubmitStatus;
using UpdateStatus = OrderBookHeap::UpdateStatus;

// Orders default to client 1. Tests that need several clients pass one explicitly.
Order limit(OrderId id, Side side, Price price, Quantity qty, ClientId client = 1,
            const char* ticker = "AAPL") {
    return Order(ticker, qty, side, OrderType::Limit, price, client, id);
}

Order market(OrderId id, Side side, Quantity qty, ClientId client = 1) {
    return Order("AAPL", qty, side, OrderType::Market, 0, client, id);
}

class HeapBookTest : public ::testing::Test {
protected:
    OrderBookHeap book{"AAPL", 64};
};

// =====================================================================================
// 1. Core behavior
// =====================================================================================

TEST_F(HeapBookTest, StartsEmpty) {
    EXPECT_EQ(book.ticker(), "AAPL");
    EXPECT_EQ(book.restingOrderCount(), 0u);
    EXPECT_EQ(book.heldMarketRemainderCount(), 0u);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.priceLevelCount(Side::Buy), 0u);
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 0u);
}

TEST_F(HeapBookTest, InvalidTickerThrows) {
    EXPECT_THROW(OrderBookHeap("", 4), std::invalid_argument);
    EXPECT_THROW(OrderBookHeap("TOOLONGXX", 4), std::invalid_argument);
}

TEST_F(HeapBookTest, WrongTickerIsIgnored) {
    const auto result = book.submitOrder(limit(1, Side::Buy, 100, 10, 1, "MSFT"));
    EXPECT_EQ(result.status, Status::IgnoredWrongTicker);
    EXPECT_EQ(result.orderId, 1u);
    EXPECT_EQ(result.remaining, 10u);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, OrdersRestWhenNothingToMatch) {
    EXPECT_EQ(book.submitOrder(limit(1, Side::Buy, 99, 10)).status, Status::RestingInBook);
    EXPECT_EQ(book.submitOrder(limit(2, Side::Sell, 101, 7)).status, Status::RestingInBook);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(99));
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(101));
    EXPECT_EQ(book.restingOrderCount(), 2u);
}

TEST_F(HeapBookTest, ExactMatchFillsBothOrders) {
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    const auto result = book.submitOrder(limit(2, Side::Buy, 100, 10, 2));
    EXPECT_EQ(result.status, Status::FilledCompletely);
    EXPECT_EQ(result.remaining, 0u);
    EXPECT_EQ(book.restingOrderCount(), 0u);
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 0u);
}

TEST_F(HeapBookTest, ExactMatchSellIncoming) {
    book.submitOrder(limit(1, Side::Buy, 100, 10, 1));
    EXPECT_EQ(book.submitOrder(limit(2, Side::Sell, 100, 10, 2)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST_F(HeapBookTest, IncomingLargerThanRestingRestsRemainder) {
    book.submitOrder(limit(1, Side::Sell, 100, 4, 1));
    const auto result = book.submitOrder(limit(2, Side::Buy, 100, 10, 2));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 6u);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(100));
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_F(HeapBookTest, IncomingSmallerThanRestingLeavesRestingPartiallyFilled) {
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    EXPECT_EQ(book.submitOrder(limit(2, Side::Buy, 100, 3, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(100));
    EXPECT_TRUE(book.cancel(1));
}

TEST_F(HeapBookTest, BuyTakesTheLowestAskFirst) {
    book.submitOrder(limit(1, Side::Sell, 102, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(3, Side::Sell, 101, 5, 1));
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 102, 5, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(101));
    EXPECT_FALSE(book.cancel(2));  // the 100 ask was filled first
}

TEST_F(HeapBookTest, SellTakesTheHighestBidFirst) {
    book.submitOrder(limit(1, Side::Buy, 98, 5, 1));
    book.submitOrder(limit(2, Side::Buy, 100, 5, 1));
    book.submitOrder(limit(3, Side::Buy, 99, 5, 1));
    EXPECT_EQ(book.submitOrder(limit(4, Side::Sell, 98, 5, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(99));
    EXPECT_FALSE(book.cancel(2));
}

TEST_F(HeapBookTest, SamePriceIsFirstInFirstOut) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(3, Side::Sell, 100, 5, 1));
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 1u);
    book.submitOrder(limit(4, Side::Buy, 100, 5, 2));
    EXPECT_FALSE(book.cancel(1));
    book.submitOrder(limit(5, Side::Buy, 100, 5, 2));
    EXPECT_FALSE(book.cancel(2));
    EXPECT_TRUE(book.cancel(3));
}

TEST_F(HeapBookTest, LargeOrderSweepsSeveralPriceLevels) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 101, 5, 1));
    book.submitOrder(limit(3, Side::Sell, 102, 5, 1));
    const auto result = book.submitOrder(limit(4, Side::Buy, 101, 12, 2));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 2u);  // 100 and 101 consumed, 102 is above the limit
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(102));
    EXPECT_EQ(book.bestBid(), std::optional<Price>(101));
}

TEST_F(HeapBookTest, MarketOrdersFillAndHoldRemainder) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 101, 5, 1));
    EXPECT_EQ(book.submitOrder(market(3, Side::Buy, 8, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(101));

    const auto held = book.submitOrder(market(4, Side::Buy, 10, 2));
    EXPECT_EQ(held.status, Status::MarketRemainderHeld);
    EXPECT_EQ(held.remaining, 8u);  // only 2 shares were left to take
    EXPECT_EQ(book.heldMarketRemainderCount(), 1u);
    EXPECT_FALSE(book.cancel(4));  // held orders are not cancelable
}

TEST_F(HeapBookTest, MarketSellFillsAgainstBids) {
    book.submitOrder(limit(1, Side::Buy, 100, 5, 1));
    EXPECT_EQ(book.submitOrder(market(2, Side::Sell, 5, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, PoolFullIsReportedAndSlotsAreReused) {
    OrderBookHeap small("AAPL", 2);
    EXPECT_EQ(small.submitOrder(limit(1, Side::Buy, 90, 1)).status, Status::RestingInBook);
    EXPECT_EQ(small.submitOrder(limit(2, Side::Buy, 91, 1)).status, Status::RestingInBook);
    const auto result = small.submitOrder(limit(3, Side::Buy, 92, 1));
    EXPECT_EQ(result.status, Status::PoolFull);
    EXPECT_EQ(result.remaining, 1u);
    EXPECT_TRUE(small.cancel(1));
    EXPECT_EQ(small.submitOrder(limit(4, Side::Buy, 93, 1)).status, Status::RestingInBook);
}

TEST_F(HeapBookTest, SlotsAreReusedAfterFills) {
    OrderBookHeap small("AAPL", 3);
    OrderId id = 1;
    for (int round = 0; round < 50; ++round) {
        EXPECT_EQ(small.submitOrder(limit(id++, Side::Sell, 100, 2, 1)).status, Status::RestingInBook);
        EXPECT_EQ(small.submitOrder(limit(id++, Side::Buy, 100, 2, 2)).status, Status::FilledCompletely);
    }
    EXPECT_EQ(small.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, CancelUnknownIdReturnsFalse) {
    EXPECT_FALSE(book.cancel(12345));
}

TEST_F(HeapBookTest, CancelOnlyOrderRemovesItsPrice) {
    book.submitOrder(limit(1, Side::Buy, 100, 5));
    EXPECT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.cancel(1));
    EXPECT_EQ(book.priceLevelCount(Side::Buy), 0u);
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST_F(HeapBookTest, CancelHeadMiddleAndTailOfOneLevel) {
    for (OrderId id = 1; id <= 4; ++id) {
        book.submitOrder(limit(id, Side::Sell, 100, 5, 1));
    }
    EXPECT_TRUE(book.cancel(2));  // middle
    EXPECT_TRUE(book.cancel(4));  // tail
    EXPECT_TRUE(book.cancel(1));  // head
    EXPECT_EQ(book.restingOrderCount(), 1u);
    EXPECT_EQ(book.submitOrder(limit(5, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, CancelingAPriceInTheMiddleOfTheHeapKeepsBestPriceCorrect) {
    for (OrderId id = 1; id <= 10; ++id) {
        book.submitOrder(limit(id, Side::Sell, 100 + static_cast<Price>(id), 1, 1));
    }
    EXPECT_TRUE(book.cancel(5));
    EXPECT_TRUE(book.cancel(9));
    EXPECT_TRUE(book.cancel(3));
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 7u);
    const std::vector<Price> expected{101, 102, 104, 106, 107, 108, 110};
    for (Price p : expected) {
        EXPECT_EQ(book.bestAsk(), std::optional<Price>(p));
        EXPECT_EQ(book.submitOrder(market(1000 + static_cast<OrderId>(p), Side::Buy, 1, 2)).status,
                  Status::FilledCompletely);
    }
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_F(HeapBookTest, CancelingBidPricesKeepsMaxOrder) {
    for (OrderId id = 1; id <= 8; ++id) {
        book.submitOrder(limit(id, Side::Buy, 100 - static_cast<Price>(id), 1, 1));
    }
    EXPECT_TRUE(book.cancel(1));  // best bid 99 removed from the top
    EXPECT_EQ(book.bestBid(), std::optional<Price>(98));
    EXPECT_TRUE(book.cancel(4));
    EXPECT_TRUE(book.cancel(8));
    EXPECT_EQ(book.bestBid(), std::optional<Price>(98));
    EXPECT_EQ(book.priceLevelCount(Side::Buy), 5u);
}

// =====================================================================================
// 2. update(orderId, newQuantity)
// =====================================================================================

// Helper: rests orders 1..n (client 1) at one sell price, oldest first, each with `qty` shares.
void restSells(OrderBookHeap& b, OrderId n, Quantity qty, Price price = 100) {
    for (OrderId id = 1; id <= n; ++id) {
        b.submitOrder(limit(id, Side::Sell, price, qty, 1));
    }
}

// Helper: a buyer from another client takes exactly one order's worth of shares at 100.
// Returns which of orders 1..candidates was at the front of the queue, i.e. the one that
// is gone afterwards. It cancels the others to find out, so use it as the last step of a test.
OrderId whoWasFirst(OrderBookHeap& b, OrderId candidates, OrderId buyId, Quantity qty) {
    EXPECT_EQ(b.submitOrder(limit(buyId, Side::Buy, 100, qty, 2)).status, Status::FilledCompletely);
    OrderId gone = 0;
    int goneCount = 0;
    for (OrderId id = 1; id <= candidates; ++id) {
        if (!b.cancel(id)) {
            gone = id;
            ++goneCount;
        }
    }
    EXPECT_EQ(goneCount, 1);
    return gone;
}

TEST_F(HeapBookTest, UpdateRejectsZeroAndUnknownOrders) {
    book.submitOrder(limit(1, Side::Sell, 100, 5));
    EXPECT_EQ(book.update(1, 0), UpdateStatus::InvalidQuantity);
    EXPECT_EQ(book.update(99, 5), UpdateStatus::NotFound);
    // The rejected zero update left the order untouched.
    EXPECT_EQ(book.submitOrder(limit(2, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
    // Filled orders are no longer resting.
    EXPECT_EQ(book.update(1, 5), UpdateStatus::NotFound);
}

TEST_F(HeapBookTest, UpdateOnAHeldMarketOrderIsNotFound) {
    EXPECT_EQ(book.submitOrder(market(1, Side::Buy, 5)).status, Status::MarketRemainderHeld);
    EXPECT_EQ(book.update(1, 3), UpdateStatus::NotFound);
}

TEST_F(HeapBookTest, UpdateToSameQuantityChangesNothing) {
    restSells(book, 2, 5);  // queue: 1, 2
    EXPECT_EQ(book.update(1, 5), UpdateStatus::Updated);
    // Order 1 kept the front of the line, so it fills first.
    EXPECT_EQ(whoWasFirst(book, 2, 10, 5), 1u);
}

TEST_F(HeapBookTest, DownsizeKeepsQueuePriority) {
    restSells(book, 2, 10);  // queue: A=1 (10), B=2 (10)
    EXPECT_EQ(book.update(1, 3), UpdateStatus::Updated);
    // A now has 3 shares and is still first: a buy of 3 fills A completely.
    EXPECT_EQ(whoWasFirst(book, 2, 10, 3), 1u);
}

TEST_F(HeapBookTest, DownsizeReducesTheQuantityThatCanTrade) {
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    EXPECT_EQ(book.update(1, 2), UpdateStatus::Updated);
    const auto result = book.submitOrder(limit(2, Side::Buy, 100, 5, 2));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 3u);  // only 2 shares were available
}

TEST_F(HeapBookTest, UpsizeMovesTheOrderToTheBackOfTheQueue) {
    restSells(book, 3, 5);  // queue: 1, 2, 3
    EXPECT_EQ(book.update(1, 8), UpdateStatus::Updated);  // order 1 was at the front
    // New queue: 2, 3, 1. Order 2 trades first, then 3, then 1.
    EXPECT_EQ(whoWasFirst(book, 3, 10, 5), 2u);
}

TEST_F(HeapBookTest, UpsizeOfAMiddleOrderGoesBehindTheOnesAfterIt) {
    // Queue 1 2 3 4: up-sizing 2 must give 1 3 4 2, not 1 4 3 2 (which a swap would give).
    restSells(book, 4, 5);
    EXPECT_EQ(book.update(2, 9), UpdateStatus::Updated);
    EXPECT_EQ(book.submitOrder(limit(10, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);  // 1
    EXPECT_FALSE(book.cancel(1));
    EXPECT_EQ(book.submitOrder(limit(11, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);  // 3
    EXPECT_FALSE(book.cancel(3));
    EXPECT_EQ(book.submitOrder(limit(12, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);  // 4
    EXPECT_FALSE(book.cancel(4));
    // Order 2 is last and now holds 9 shares.
    EXPECT_EQ(book.submitOrder(limit(13, Side::Buy, 100, 9, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, UpsizeOfTheLastOrderAndOfTheOnlyOrderKeepsThem) {
    restSells(book, 3, 5);
    EXPECT_EQ(book.update(3, 7), UpdateStatus::Updated);  // already last
    EXPECT_EQ(whoWasFirst(book, 3, 10, 5), 1u);

    OrderBookHeap other("AAPL", 8);
    other.submitOrder(limit(1, Side::Buy, 100, 5, 1));
    EXPECT_EQ(other.update(1, 9), UpdateStatus::Updated);  // the only order at its price
    EXPECT_EQ(other.submitOrder(limit(2, Side::Sell, 100, 9, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(other.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, UpsizeWorksOnTheBuySideToo) {
    book.submitOrder(limit(1, Side::Buy, 100, 5, 1));
    book.submitOrder(limit(2, Side::Buy, 100, 5, 1));
    EXPECT_EQ(book.update(1, 6), UpdateStatus::Updated);  // queue becomes 2, 1
    EXPECT_EQ(book.submitOrder(limit(3, Side::Sell, 100, 5, 2)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.cancel(2));
    EXPECT_TRUE(book.cancel(1));
}

TEST_F(HeapBookTest, UpdateAfterAPartialFillSetsTheRemainingQuantity) {
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 10, 1));
    book.submitOrder(limit(3, Side::Buy, 100, 4, 2));  // order 1 now has 6 left
    EXPECT_EQ(book.update(1, 9), UpdateStatus::Updated);  // up-size: goes behind order 2
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 100, 10, 2)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.cancel(2));
    EXPECT_EQ(book.submitOrder(limit(5, Side::Buy, 100, 9, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, UpsizeLeavesOtherPricesAndTheHeapAlone) {
    book.submitOrder(limit(1, Side::Sell, 103, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 101, 5, 1));
    book.submitOrder(limit(3, Side::Sell, 101, 5, 1));
    book.submitOrder(limit(4, Side::Sell, 105, 5, 1));
    EXPECT_EQ(book.update(2, 8), UpdateStatus::Updated);
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(101));
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 3u);
}

TEST_F(HeapBookTest, UpdateTooLargeThrowsAndLeavesTheOrderAlone) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(3, Side::Buy, 100, 2, 2));  // order 1 is partially filled: 3 left
    // filled (2) + 4,294,967,295 does not fit in 32 bits.
    EXPECT_THROW(book.update(1, std::numeric_limits<Quantity>::max()), std::invalid_argument);
    EXPECT_EQ(book.restingOrderCount(), 2u);
    // Order 1 still has its place and its 3 shares.
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 100, 3, 2)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.cancel(1));
}

// =====================================================================================
// 3. disconnect(clientId)
// =====================================================================================

TEST_F(HeapBookTest, DisconnectOfAClientWithNoOrdersDoesNothing) {
    book.submitOrder(limit(1, Side::Buy, 99, 5, 1));
    EXPECT_EQ(book.disconnect(7), 0u);
    EXPECT_EQ(book.restingOrderCount(), 1u);
}

TEST_F(HeapBookTest, DisconnectCancelsAllOfTheClientsRestingOrdersOnBothSides) {
    book.submitOrder(limit(1, Side::Buy, 98, 5, 1));
    book.submitOrder(limit(2, Side::Buy, 99, 5, 2));
    book.submitOrder(limit(3, Side::Buy, 99, 5, 1));
    book.submitOrder(limit(4, Side::Sell, 101, 5, 1));
    book.submitOrder(limit(5, Side::Sell, 102, 5, 2));
    EXPECT_EQ(book.disconnect(1), 3u);
    EXPECT_EQ(book.restingOrderCount(), 2u);
    EXPECT_FALSE(book.cancel(1));
    EXPECT_FALSE(book.cancel(3));
    EXPECT_FALSE(book.cancel(4));
    EXPECT_TRUE(book.cancel(2));
    EXPECT_TRUE(book.cancel(5));
}

TEST_F(HeapBookTest, DisconnectRemovesEmptiedPricesAndKeepsTheHeapValid) {
    // Client 1 owns prices 101, 103, 105 (middle of the heap); client 2 owns 102, 104, 106.
    for (Price p = 101; p <= 106; ++p) {
        book.submitOrder(limit(static_cast<OrderId>(p), Side::Sell, p, 1, (p % 2) ? 1 : 2));
    }
    EXPECT_EQ(book.disconnect(1), 3u);
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 3u);
    for (Price p : {102, 104, 106}) {
        EXPECT_EQ(book.bestAsk(), std::optional<Price>(p));
        EXPECT_EQ(book.submitOrder(market(1000 + static_cast<OrderId>(p), Side::Buy, 1, 3)).status,
                  Status::FilledCompletely);
    }
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_F(HeapBookTest, DisconnectKeepsTheQueueOrderOfTheOthers) {
    // One price, queue: 1 (client 1), 2 (client 2), 3 (client 1), 4 (client 2).
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 2));
    book.submitOrder(limit(3, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(4, Side::Sell, 100, 5, 2));
    EXPECT_EQ(book.disconnect(1), 2u);
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 1u);
    EXPECT_EQ(book.submitOrder(limit(10, Side::Buy, 100, 5, 3)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.cancel(2));  // 2 was first and traded
    EXPECT_TRUE(book.cancel(4));
}

TEST_F(HeapBookTest, DisconnectFreesHeldMarketOrdersAndTheirPoolSlots) {
    OrderBookHeap small("AAPL", 2);
    EXPECT_EQ(small.submitOrder(market(1, Side::Buy, 5, 1)).status, Status::MarketRemainderHeld);
    EXPECT_EQ(small.submitOrder(market(2, Side::Buy, 5, 2)).status, Status::MarketRemainderHeld);
    EXPECT_EQ(small.submitOrder(limit(3, Side::Buy, 90, 1, 3)).status, Status::PoolFull);

    EXPECT_EQ(small.disconnect(1), 1u);
    EXPECT_EQ(small.heldMarketRemainderCount(), 1u);  // client 2's held order stays
    // The freed slot can be used again.
    EXPECT_EQ(small.submitOrder(limit(4, Side::Buy, 90, 1, 3)).status, Status::RestingInBook);
}

TEST_F(HeapBookTest, ADisconnectedClientMayStillSubmitNewOrders) {
    book.submitOrder(limit(1, Side::Buy, 99, 5, 1));
    EXPECT_EQ(book.disconnect(1), 1u);
    EXPECT_EQ(book.submitOrder(limit(2, Side::Buy, 99, 5, 1)).status, Status::RestingInBook);
    EXPECT_EQ(book.restingOrderCount(), 1u);
}

// =====================================================================================
// 4. Self-trade prevention (decrement-and-cancel)
// =====================================================================================

TEST_F(HeapBookTest, SmallerIncomingOrderIsRemovedAndTheRestingOrderKeepsTheRest) {
    // Client 1: resting sell 10, then a buy of 4. Nothing trades: the buy disappears
    // and the sell keeps 6, still in its original spot.
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 2));  // client 2 is behind client 1
    const auto result = book.submitOrder(limit(3, Side::Buy, 100, 4, 1));
    EXPECT_EQ(result.status, Status::CanceledBySelfTrade);
    EXPECT_EQ(result.remaining, 0u);
    EXPECT_EQ(book.restingOrderCount(), 2u);
    EXPECT_FALSE(book.bestBid().has_value());  // the buy did not rest

    // The sell kept 6 shares and the front spot: client 3 buying 8 takes 6 from client 1,
    // then 2 from client 2.
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 100, 8, 3)).status, Status::FilledCompletely);
    EXPECT_FALSE(book.cancel(1));
    EXPECT_TRUE(book.cancel(2));  // client 2 still has 3 left
}

TEST_F(HeapBookTest, LargerIncomingOrderKeepsItsRemainderAndMatchesTheNextOrder) {
    // Client 1: resting sell 4. Client 2: resting sell 5 behind it. Client 1 buys 10.
    // 4 is removed from both of client 1's orders, then the remaining 6 trades with
    // client 2 (5 shares), and the last 1 rests as a bid.
    book.submitOrder(limit(1, Side::Sell, 100, 4, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 2));
    const auto result = book.submitOrder(limit(3, Side::Buy, 100, 10, 1));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 1u);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(100));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.restingOrderCount(), 1u);
}

TEST_F(HeapBookTest, EqualQuantitiesRemoveBothOrders) {
    book.submitOrder(limit(1, Side::Sell, 100, 6, 1));
    const auto result = book.submitOrder(limit(2, Side::Buy, 100, 6, 1));
    EXPECT_EQ(result.status, Status::CanceledBySelfTrade);
    EXPECT_EQ(book.restingOrderCount(), 0u);
    EXPECT_EQ(book.priceLevelCount(Side::Sell), 0u);
}

TEST_F(HeapBookTest, SelfTradePreventionWorksFromTheSellSideToo) {
    book.submitOrder(limit(1, Side::Buy, 100, 10, 1));
    EXPECT_EQ(book.submitOrder(limit(2, Side::Sell, 100, 4, 1)).status, Status::CanceledBySelfTrade);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(100));
    EXPECT_EQ(book.submitOrder(limit(3, Side::Sell, 100, 6, 2)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, IncomingOrderDoesNotSkipItsOwnOrderToReachSomeoneBehindIt) {
    // Queue at 100: client 1 sell 5 (front), client 2 sell 5 (behind).
    // Client 1 buys 3. Price-time priority: the buy meets client 1's own order first.
    // It must not jump to client 2.
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 2));
    EXPECT_EQ(book.submitOrder(limit(3, Side::Buy, 100, 3, 1)).status, Status::CanceledBySelfTrade);
    // Client 2 was untouched: it still has all 5 shares, behind client 1's remaining 2.
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 100, 7, 3)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, SelfTradeRemovalThenTradeWithAnotherClient) {
    // Queue at 100: client 1 sell 5, client 2 sell 5. Client 1 buys 8:
    // 5 are removed from both of client 1's orders, then 3 trade with client 2.
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    book.submitOrder(limit(2, Side::Sell, 100, 5, 2));
    const auto result = book.submitOrder(limit(3, Side::Buy, 100, 8, 1));
    EXPECT_EQ(result.status, Status::CanceledBySelfTrade);  // part of it was removed, none is left
    EXPECT_EQ(result.remaining, 0u);
    EXPECT_EQ(book.restingOrderCount(), 1u);  // client 2 has 2 left
    EXPECT_EQ(book.submitOrder(limit(4, Side::Buy, 100, 2, 3)).status, Status::FilledCompletely);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, SelfTradePreventionInTheMiddleOfASweepAcrossPrices) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 2));
    book.submitOrder(limit(2, Side::Sell, 101, 6, 1));  // client 1's own order
    book.submitOrder(limit(3, Side::Sell, 102, 5, 3));
    // Client 1 buys 20 up to 105: trade 5 at 100, remove 6 at 101, trade 5 at 102, rest 4.
    const auto result = book.submitOrder(limit(4, Side::Buy, 105, 20, 1));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 4u);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(105));
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_F(HeapBookTest, OrdersFromTheSameClientAtNonCrossingPricesBothRest) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    EXPECT_EQ(book.submitOrder(limit(2, Side::Buy, 99, 5, 1)).status, Status::RestingInBook);
    EXPECT_EQ(book.bestBid(), std::optional<Price>(99));
    EXPECT_EQ(book.bestAsk(), std::optional<Price>(100));
}

TEST_F(HeapBookTest, SelfTradePreventionAppliesToMarketOrders) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    EXPECT_EQ(book.submitOrder(market(2, Side::Buy, 3, 1)).status, Status::CanceledBySelfTrade);

    // 2 shares are left of client 1's sell. A market buy of 5 removes those 2 and the
    // last 3 shares have nothing to trade with, so they are held.
    const auto held = book.submitOrder(market(3, Side::Buy, 5, 1));
    EXPECT_EQ(held.status, Status::MarketRemainderHeld);
    EXPECT_EQ(held.remaining, 3u);
    EXPECT_EQ(book.heldMarketRemainderCount(), 1u);
    EXPECT_EQ(book.restingOrderCount(), 0u);
}

TEST_F(HeapBookTest, DifferentClientsStillTrade) {
    book.submitOrder(limit(1, Side::Sell, 100, 5, 1));
    EXPECT_EQ(book.submitOrder(limit(2, Side::Buy, 100, 5, 2)).status, Status::FilledCompletely);
}

TEST_F(HeapBookTest, SelfTradeRemovalIsNotCountedAsAFill) {
    // The remaining 6 shares of client 1's sell are still real and tradeable.
    book.submitOrder(limit(1, Side::Sell, 100, 10, 1));
    book.submitOrder(limit(2, Side::Buy, 100, 4, 1));  // removes 4 from both
    const auto result = book.submitOrder(limit(3, Side::Buy, 100, 9, 2));
    EXPECT_EQ(result.status, Status::RestingInBook);
    EXPECT_EQ(result.remaining, 3u);  // 6 traded, 3 left over
}

// =====================================================================================
// 5. Order::setRemaining
// =====================================================================================

TEST(OrderSetRemaining, KeepsFilledCorrect) {
    Order order("AAPL", 10, Side::Buy, OrderType::Limit, 100, 1, 1);
    order.fill(4);  // filled 4, remaining 6
    order.setRemaining(2);
    EXPECT_EQ(order.remaining, 2u);
    EXPECT_EQ(order.size, 6u);
    EXPECT_EQ(order.filled(), 4u);
    order.setRemaining(9);
    EXPECT_EQ(order.size, 13u);
    EXPECT_EQ(order.filled(), 4u);
    order.setRemaining(0);
    EXPECT_TRUE(order.isFilled());
    EXPECT_THROW(order.setRemaining(std::numeric_limits<Quantity>::max()), std::invalid_argument);
}

// =====================================================================================
// 6. Randomized comparison against a simple reference book
// =====================================================================================
// The reference book keeps every resting order in a plain list and, for every match,
// searches the whole list for the best price, breaking ties by arrival sequence. It is slow
// but easy to trust. The heap book must give the same answer to every operation.

class ReferenceBook {
public:
    struct Resting {
        OrderId id;
        ClientId client;
        Side side;
        Price price;
        Quantity remaining;
        std::uint64_t sequence;  // lower = earlier in line
    };

    std::pair<Status, Quantity> submit(OrderId id, ClientId client, Side side, OrderType type,
                                       Price price, Quantity quantity) {
        Quantity remaining = quantity;
        Quantity selfTradeCanceled = 0;
        std::vector<Resting>& opposite = (side == Side::Buy) ? sells_ : buys_;

        while (remaining > 0) {
            const auto best = bestOf(opposite);
            if (best == opposite.end()) break;
            const bool priceOk = type == OrderType::Market ||
                                 (side == Side::Buy ? best->price <= price : best->price >= price);
            if (!priceOk) break;

            const Quantity amount = std::min(remaining, best->remaining);
            remaining -= amount;
            best->remaining -= amount;
            if (best->client == client) {
                selfTradeCanceled += amount;  // removed from both, no trade
            }
            if (best->remaining == 0) {
                opposite.erase(best);
            }
        }

        if (remaining == 0) {
            return {selfTradeCanceled > 0 ? Status::CanceledBySelfTrade : Status::FilledCompletely, 0};
        }
        if (type == OrderType::Limit) {
            (side == Side::Buy ? buys_ : sells_)
                .push_back(Resting{id, client, side, price, remaining, ++sequence_});
            return {Status::RestingInBook, remaining};
        }
        held_.push_back({id, client});
        return {Status::MarketRemainderHeld, remaining};
    }

    bool cancel(OrderId id) {
        return eraseById(buys_, id) || eraseById(sells_, id);
    }

    UpdateStatus update(OrderId id, Quantity newQuantity) {
        if (newQuantity == 0) return UpdateStatus::InvalidQuantity;
        for (std::vector<Resting>* side : {&buys_, &sells_}) {
            for (Resting& r : *side) {
                if (r.id != id) continue;
                if (newQuantity > r.remaining) r.sequence = ++sequence_;  // back of the line
                r.remaining = newQuantity;
                return UpdateStatus::Updated;
            }
        }
        return UpdateStatus::NotFound;
    }

    std::size_t disconnect(ClientId client) {
        std::size_t removed = 0;
        for (std::vector<Resting>* side : {&buys_, &sells_}) {
            const auto before = side->size();
            side->erase(std::remove_if(side->begin(), side->end(),
                                       [&](const Resting& r) { return r.client == client; }),
                        side->end());
            removed += before - side->size();
        }
        const auto heldBefore = held_.size();
        held_.erase(std::remove_if(held_.begin(), held_.end(),
                                   [&](const auto& h) { return h.second == client; }),
                    held_.end());
        return removed + (heldBefore - held_.size());
    }

    std::size_t restingCount() const { return buys_.size() + sells_.size(); }
    std::size_t heldCount() const { return held_.size(); }
    std::optional<Price> bestBid() const {
        if (buys_.empty()) return std::nullopt;
        Price best = buys_.front().price;
        for (const Resting& r : buys_) best = std::max(best, r.price);
        return best;
    }
    std::optional<Price> bestAsk() const {
        if (sells_.empty()) return std::nullopt;
        Price best = sells_.front().price;
        for (const Resting& r : sells_) best = std::min(best, r.price);
        return best;
    }
    std::size_t levelCount(Side side) const {
        std::set<Price> prices;
        for (const Resting& r : (side == Side::Buy ? buys_ : sells_)) prices.insert(r.price);
        return prices.size();
    }

private:
    // Best order of one side: best price first (highest for buys, lowest for sells), then the
    // earliest sequence. The side is found from the orders themselves.
    static std::vector<Resting>::iterator bestOf(std::vector<Resting>& orders) {
        auto best = orders.end();
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (best == orders.end()) {
                best = it;
                continue;
            }
            const bool betterPrice = (it->side == Side::Buy) ? it->price > best->price
                                                             : it->price < best->price;
            if (betterPrice || (it->price == best->price && it->sequence < best->sequence)) {
                best = it;
            }
        }
        return best;
    }

    static bool eraseById(std::vector<Resting>& orders, OrderId id) {
        const auto it = std::find_if(orders.begin(), orders.end(),
                                     [id](const Resting& r) { return r.id == id; });
        if (it == orders.end()) return false;
        orders.erase(it);
        return true;
    }

    std::vector<Resting> buys_;
    std::vector<Resting> sells_;
    std::vector<std::pair<OrderId, ClientId>> held_;
    std::uint64_t sequence_ = 0;
};

TEST(HeapBookRandomized, MatchesTheReferenceBookOnEveryOperation) {
    OrderBookHeap book("AAPL", 50000);
    ReferenceBook reference;
    std::mt19937_64 rng(424242);
    OrderId nextId = 1;

    for (int step = 0; step < 30000; ++step) {
        const auto roll = rng() % 100;
        if (nextId > 1 && roll < 8) {  // cancel
            const OrderId id = 1 + rng() % (nextId - 1);
            ASSERT_EQ(book.cancel(id), reference.cancel(id)) << "step " << step;
        } else if (nextId > 1 && roll < 25) {  // update (0 is included on purpose)
            const OrderId id = 1 + rng() % (nextId - 1);
            const Quantity quantity = static_cast<Quantity>(rng() % 40);
            ASSERT_EQ(book.update(id, quantity), reference.update(id, quantity)) << "step " << step;
        } else if (roll < 28) {  // disconnect
            const ClientId client = 1 + rng() % 4;
            ASSERT_EQ(book.disconnect(client), reference.disconnect(client)) << "step " << step;
        } else {  // submit: few clients and a narrow price range, so self-trades are common
            const OrderId id = nextId++;
            const ClientId client = 1 + rng() % 4;
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            const Quantity quantity = static_cast<Quantity>(1 + rng() % 30);
            const bool isMarket = (rng() % 25) == 0;
            const Price price = static_cast<Price>(95 + rng() % 11);
            const Order order = isMarket ? market(id, side, quantity, client)
                                         : limit(id, side, price, quantity, client);
            const auto got = book.submitOrder(order);
            const auto want = reference.submit(id, client, side,
                                               isMarket ? OrderType::Market : OrderType::Limit,
                                               price, quantity);
            ASSERT_EQ(static_cast<int>(got.status), static_cast<int>(want.first)) << "step " << step;
            ASSERT_EQ(got.remaining, want.second) << "step " << step;
        }

        ASSERT_EQ(book.restingOrderCount(), reference.restingCount()) << "step " << step;
        ASSERT_EQ(book.heldMarketRemainderCount(), reference.heldCount()) << "step " << step;
        ASSERT_EQ(book.bestBid(), reference.bestBid()) << "step " << step;
        ASSERT_EQ(book.bestAsk(), reference.bestAsk()) << "step " << step;
        ASSERT_EQ(book.priceLevelCount(Side::Buy), reference.levelCount(Side::Buy)) << "step " << step;
        ASSERT_EQ(book.priceLevelCount(Side::Sell), reference.levelCount(Side::Sell)) << "step " << step;
        // The book must never be left crossed: self-trade prevention removes overlaps.
        if (book.bestBid() && book.bestAsk()) {
            ASSERT_LT(*book.bestBid(), *book.bestAsk()) << "crossed book at step " << step;
        }
    }
}

}  // namespace
#include "OrderBook.h"

#include <cassert>
#include <string>

namespace {

void testLimitOrderRestsWhenPricesDoNotCross() {
    OrderBook book("AAPL", 16);
    book.submitOrder(Order("AAPL", 8, Side::Sell, OrderType::Limit, 10001, 1, 101));
    const auto result = book.submitOrder(
        Order("AAPL", 7, Side::Buy, OrderType::Limit, 10000, 2, 102));

    assert(result.status == OrderBook::SubmitStatus::RestingInBook);
    assert(result.remaining == 7);
    assert(book.restingOrderCount() == 2);
    const std::string view = book.toString();
    assert(view.find("Price 10000") != std::string::npos);
    assert(view.find("Price 10001") != std::string::npos);
}

void testPartialFillAndFifoAtSamePrice() {
    OrderBook book("AAPL", 16);
    book.submitOrder(Order("AAPL", 40, Side::Sell, OrderType::Limit, 10000, 1, 201));
    book.submitOrder(Order("AAPL", 30, Side::Sell, OrderType::Limit, 10000, 2, 202));

    const auto result = book.submitOrder(
        Order("AAPL", 60, Side::Buy, OrderType::Limit, 10000, 3, 203));

    assert(result.status == OrderBook::SubmitStatus::FilledCompletely);
    assert(book.restingOrderCount() == 1);
    assert(!book.cancel(201)); // First sell order was completely filled.
    assert(book.cancel(202));  // Second sell order has 10 shares left.
    assert(book.restingOrderCount() == 0);
}

void testMarketRemainderIsHeldOutsideTheBook() {
    OrderBook book("AAPL", 16);
    book.submitOrder(Order("AAPL", 60, Side::Sell, OrderType::Limit, 10000, 1, 301));
    book.submitOrder(Order("AAPL", 40, Side::Sell, OrderType::Limit, 10001, 2, 302));

    const auto result = book.submitOrder(
        Order("AAPL", 200, Side::Buy, OrderType::Market, 0, 3, 303));

    assert(result.status == OrderBook::SubmitStatus::MarketRemainderHeld);
    assert(result.remaining == 100);
    assert(book.heldMarketRemainderCount() == 1);
    assert(book.restingOrderCount() == 0);
    assert(!book.cancel(303)); // Held orders are not in the resting-order lookup.
}

void testWrongTickerIsIgnored() {
    OrderBook book("AAPL", 16);
    const auto result = book.submitOrder(
        Order("MSFT", 10, Side::Buy, OrderType::Limit, 10000, 1, 401));

    assert(result.status == OrderBook::SubmitStatus::IgnoredWrongTicker);
    assert(book.restingOrderCount() == 0);
}

} // namespace

int main() {
    testLimitOrderRestsWhenPricesDoNotCross();
    testPartialFillAndFifoAtSamePrice();
    testMarketRemainderIsHeldOutsideTheBook();
    testWrongTickerIsIgnored();
}

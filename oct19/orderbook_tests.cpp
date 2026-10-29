#include "map/OrderBookMap.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int checksRun = 0;
int checksFailed = 0;

const char* statusName(OrderBook::SubmitStatus status) {
    switch (status) {
        case OrderBook::SubmitStatus::RestingInBook:
            return "RestingInBook";
        case OrderBook::SubmitStatus::FilledCompletely:
            return "FilledCompletely";
        case OrderBook::SubmitStatus::IgnoredWrongTicker:
            return "IgnoredWrongTicker";
        case OrderBook::SubmitStatus::DuplicateActiveOrderId:
            return "DuplicateActiveOrderId";
        case OrderBook::SubmitStatus::PoolFull:
            return "PoolFull";
        case OrderBook::SubmitStatus::MarketRemainderHeld:
            return "MarketRemainderHeld";
    }

    return "UnknownStatus";
}

void check(bool condition, const std::string& description) {
    ++checksRun;
    if (condition) {
        std::cout << "  [PASS] " << description << '\n';
    } else {
        ++checksFailed;
        std::cout << "  [FAIL] " << description << '\n';
    }
}

std::size_t countOccurrences(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = 0;

    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }

    return count;
}

std::string sellSection(const std::string& bookView) {
    const auto start = bookView.find("SELLS (lowest price first)");
    if (start == std::string::npos) {
        return {};
    }
    return bookView.substr(start);
}

// Matches the first three transitions in the supplied picture in order.
// The market-sell transition at the bottom of the picture is intentionally
// omitted because that behavior is not part of the current test scope.
void testPictureTransitionsOneThroughThree() {
    std::cout << "\n[TEST] Picture transitions 1-3: add bid, add ask, partially fill and rest remainder\n";
    OrderBook book("AAPL", 16);

    std::cout << "  Setting up the initial book:\n";
    std::cout << "    SELL 8 @ 10001 (order 101)\n";
    const auto initialAsk = book.submitOrder(
        Order("AAPL", 8, Side::Sell, OrderType::Limit, 10001, 1, 101));
    std::cout << "    BUY 5 @ 10000 (order 102)\n";
    const auto initialBid = book.submitOrder(
        Order("AAPL", 5, Side::Buy, OrderType::Limit, 10000, 2, 102));

    check(initialAsk.status == OrderBook::SubmitStatus::RestingInBook,
          "Initial 8-share ask rests at 10001");
    check(initialBid.status == OrderBook::SubmitStatus::RestingInBook,
          "Initial 5-share bid rests at 10000");
    std::cout << "  Initial order book:\n" << book.toString();

    // Picture transition 1: add bid @ 100.00, size 7.
    std::cout << "\n  [TRANSITION 1] Add BUY 7 @ 10000 (order 103)\n";
    const auto addBid = book.submitOrder(
        Order("AAPL", 7, Side::Buy, OrderType::Limit, 10000, 3, 103));
    const std::string afterBid = book.toString();
    std::cout << "  Result: status=" << statusName(addBid.status)
              << ", remaining=" << addBid.remaining
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << afterBid;

    check(addBid.status == OrderBook::SubmitStatus::RestingInBook,
          "New bid does not cross the 10001 ask and rests in the book");
    check(addBid.remaining == 7,
          "All 7 shares remain unfilled after adding the bid");
    check(book.restingOrderCount() == 3,
          "Three orders are resting after transition 1");
    check(countOccurrences(afterBid, "Price 10000:") == 1,
          "The existing 10000 bid level is reused instead of creating a duplicate level");
    check(afterBid.find("Order #102") < afterBid.find("Order #103"),
          "The older 5-share bid (102) remains ahead of the new 7-share bid (103), preserving FIFO");
    check(afterBid.find("Order #101") != std::string::npos,
          "The 8-share ask at 10001 remains untouched");

    // Picture transition 2: add ask @ 100.02, size 3.
    std::cout << "\n  [TRANSITION 2] Add SELL 3 @ 10002 (order 104)\n";
    const auto addAsk = book.submitOrder(
        Order("AAPL", 3, Side::Sell, OrderType::Limit, 10002, 4, 104));
    const std::string afterAsk = book.toString();
    const std::string asksAfterAsk = sellSection(afterAsk);
    std::cout << "  Result: status=" << statusName(addAsk.status)
              << ", remaining=" << addAsk.remaining
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << afterAsk;

    check(addAsk.status == OrderBook::SubmitStatus::RestingInBook,
          "New ask at 10002 does not cross the best bid at 10000");
    check(addAsk.remaining == 3,
          "All 3 shares remain on the new ask at 10002");
    check(book.restingOrderCount() == 4,
          "Four orders are resting after transition 2");
    check(asksAfterAsk.find("Price 10001:") != std::string::npos &&
              asksAfterAsk.find("Price 10002:") != std::string::npos,
          "The sell side contains both the 10001 and 10002 price levels");
    check(asksAfterAsk.find("Price 10001:") < asksAfterAsk.find("Price 10002:"),
          "Sell price levels are displayed from lowest ask to highest ask");

    // Picture transition 3: buy 10 @ 100.01. It fills the 8-share ask,
    // then rests its remaining 2 shares as a bid at 100.01.
    std::cout << "\n  [TRANSITION 3] Add BUY 10 @ 10001 (order 105)\n";
    const auto partialIncoming = book.submitOrder(
        Order("AAPL", 10, Side::Buy, OrderType::Limit, 10001, 5, 105));
    const std::string afterPartialFill = book.toString();
    const std::string asksAfterFill = sellSection(afterPartialFill);
    std::cout << "  Result: status=" << statusName(partialIncoming.status)
              << ", remaining=" << partialIncoming.remaining
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << afterPartialFill;

    check(partialIncoming.status == OrderBook::SubmitStatus::RestingInBook,
          "Incoming buy fills the eligible ask and rests its leftover limit quantity");
    check(partialIncoming.remaining == 2,
          "The incoming 10-share buy has exactly 2 shares remaining after filling 8");
    check(afterPartialFill.find("Order #101") == std::string::npos,
          "The fully filled 8-share ask (order 101) is removed from the book");
    check(afterPartialFill.find("Order #105") != std::string::npos &&
              afterPartialFill.find("remaining 2/10") != std::string::npos,
          "The incoming order rests with 2 of its original 10 shares remaining");
    check(book.restingOrderCount() == 4,
          "Four resting orders remain after one ask is removed and the incoming remainder rests");
    check(asksAfterFill.find("Price 10001:") == std::string::npos &&
              asksAfterFill.find("Price 10002:") != std::string::npos,
          "The empty 10001 ask level is removed while the 10002 ask remains");
    check(afterPartialFill.find("Price 10001:") < afterPartialFill.find("Price 10000:"),
          "The new 10001 bid is the best bid and appears before the 10000 bid level");
    check(afterPartialFill.find("Order #102") < afterPartialFill.find("Order #103"),
          "The original 10000 bids retain their FIFO order");
    check(afterPartialFill.find("Order #104") != std::string::npos,
          "The 3-share ask at 10002 remains unfilled");
}

// Additional case: FIFO matching at one price, including removal of a filled
// resting order and cancellation of the partially filled order.
void testFifoMatchingAndCancellation() {
    std::cout << "\n[TEST] Additional case: FIFO matching and cancellation\n";
    OrderBook book("AAPL", 16);

    std::cout << "  Submitting SELL 40 @ 10000 (order 201)\n";
    book.submitOrder(Order("AAPL", 40, Side::Sell, OrderType::Limit, 10000, 1, 201));
    std::cout << "  Submitting SELL 30 @ 10000 (order 202)\n";
    book.submitOrder(Order("AAPL", 30, Side::Sell, OrderType::Limit, 10000, 2, 202));
    std::cout << "  Submitting BUY 60 @ 10000 (order 203)\n";
    const auto result = book.submitOrder(
        Order("AAPL", 60, Side::Buy, OrderType::Limit, 10000, 3, 203));

    std::cout << "  Result: status=" << statusName(result.status)
              << ", remaining=" << result.remaining
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << book.toString();

    check(result.status == OrderBook::SubmitStatus::FilledCompletely,
          "The incoming 60-share buy is completely filled");
    check(book.restingOrderCount() == 1,
          "Only the partially filled second sell order remains");
    check(book.toString().find("Order #201") == std::string::npos,
          "The first sell order is removed after being completely filled");
    check(book.toString().find("Order #202") != std::string::npos &&
              book.toString().find("remaining 10/30") != std::string::npos,
          "The second sell order remains with 10 of its original 30 shares");

    std::cout << "  Cancelling 201 (expected: FAIL because it was fully filled)\n";
    const bool canceled201 = book.cancel(201);
    std::cout << "  Actual cancel() return: " << (canceled201 ? "true" : "false")
              << " (expected false)\n";
    check(!canceled201, "A fully filled order cannot be canceled");

    std::cout << "  Cancelling 202 (expected: SUCCESS because 10 shares remain)\n";
    const bool canceled202 = book.cancel(202);
    std::cout << "  Actual cancel() return: " << (canceled202 ? "true" : "false")
              << " (expected true)\n";
    check(canceled202, "A partially filled resting order can be canceled");
    check(book.restingOrderCount() == 0,
          "The book is empty after the remaining resting order is canceled");
}

// Additional case: test the current pseudocode's explicitly held market-buy
// remainder. This is not the market-sell transition shown at the bottom of the picture.
void testMarketBuyRemainderIsHeld() {
    std::cout << "\n[TEST] Additional case: market-buy remainder is held outside the book\n";
    OrderBook book("AAPL", 16);

    std::cout << "  Submitting SELL 60 @ 10000 (order 301)\n";
    book.submitOrder(Order("AAPL", 60, Side::Sell, OrderType::Limit, 10000, 1, 301));
    std::cout << "  Submitting SELL 40 @ 10001 (order 302)\n";
    book.submitOrder(Order("AAPL", 40, Side::Sell, OrderType::Limit, 10001, 2, 302));
    std::cout << "  Submitting MARKET BUY 200 (order 303)\n";
    const auto result = book.submitOrder(
        Order("AAPL", 200, Side::Buy, OrderType::Market, 0, 3, 303));

    std::cout << "  Result: status=" << statusName(result.status)
              << ", remaining=" << result.remaining
              << ", held market orders=" << book.heldMarketRemainderCount()
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << book.toString();

    check(result.status == OrderBook::SubmitStatus::MarketRemainderHeld,
          "The unfilled market-buy remainder is held per current pseudocode");
    check(result.remaining == 100,
          "100 shares remain after consuming the 100 available shares");
    check(book.heldMarketRemainderCount() == 1,
          "One market order is held outside the book");
    check(book.restingOrderCount() == 0,
          "The held market order is not counted as a resting order");
    std::cout << "  Cancelling held order 303 (expected: FAIL; it is not resting)\n";
    const bool canceled303 = book.cancel(303);
    std::cout << "  Actual cancel() return: " << (canceled303 ? "true" : "false")
              << " (expected false)\n";
    check(!canceled303, "A held market order is not cancelable through the resting-order map");
}

// Additional case: an order for another ticker must not enter this book.
void testWrongTickerIsIgnored() {
    std::cout << "\n[TEST] Additional case: wrong ticker is ignored\n";
    OrderBook book("AAPL", 16);

    std::cout << "  Submitting BUY 10 MSFT @ 10000 (order 401) to an AAPL book\n";
    const auto result = book.submitOrder(
        Order("MSFT", 10, Side::Buy, OrderType::Limit, 10000, 1, 401));
    std::cout << "  Result: status=" << statusName(result.status)
              << ", resting orders=" << book.restingOrderCount() << '\n';
    std::cout << book.toString();

    check(result.status == OrderBook::SubmitStatus::IgnoredWrongTicker,
          "The MSFT order is ignored by the AAPL order book");
    check(book.restingOrderCount() == 0,
          "The wrong-ticker order is not added to the book");
}

} // namespace

int main() {
    std::cout << "Running OrderBook tests...\n";
    std::cout << "Prices are shown as integer ticks (10001 represents $100.01 if one tick is $0.01).\n";

    testPictureTransitionsOneThroughThree();
    testFifoMatchingAndCancellation();
    testMarketBuyRemainderIsHeld();
    testWrongTickerIsIgnored();

    std::cout << "\n========================================\n";
    std::cout << "Checks run: " << checksRun << '\n';
    std::cout << "Checks failed: " << checksFailed << '\n';
    if (checksFailed == 0) {
        std::cout << "[PASS] All OrderBook tests passed.\n";
        return 0;
    }

    std::cout << "[FAIL] Some OrderBook tests failed.\n";
    return 1;
}

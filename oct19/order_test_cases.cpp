#include "order.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(bool condition, std::string_view description) {
    ++checksRun;
    if (condition) {
        std::cout << "[PASS] " << description << '\n';
    } else {
        ++checksFailed;
        std::cout << "[FAIL] " << description << '\n';
    }
}

template <typename Function>
void expectInvalidArgument(Function&& function, std::string_view description) {
    try {
        std::forward<Function>(function)();
        check(false, description);
    } catch (const std::invalid_argument&) {
        check(true, description);
    } catch (...) {
        check(false, description);
    }
}

void testFilledQuantityAndFillState() {
    std::cout << "\n[TEST] filled(), remaining, and isFilled()\n";

    Order order("AAPL", 10, Side::Buy, OrderType::Limit, 10000, 1, 101);
    check(order.filled() == 0, "filled() is zero before any shares are filled");
    check(order.remaining == 10, "remaining starts at the submitted quantity");
    check(!order.isFilled(), "isFilled() is false before any shares are filled");

    check(order.fill(4) == 4, "fill(4) returns the quantity filled");
    check(order.remaining == 6, "remaining is reduced after a partial fill");
    check(order.filled() == 4, "filled() reports the partial-fill quantity");
    check(!order.isFilled(), "isFilled() remains false after a partial fill");

    check(order.fill(6) == 6, "fill(6) fills the remaining quantity");
    check(order.remaining == 0, "remaining is zero after a complete fill");
    check(order.filled() == 10, "filled() reports the original quantity after a complete fill");
    check(order.isFilled(), "isFilled() is true after a complete fill");
}

void testOverfillThrowsAndPreservesRemaining() {
    std::cout << "\n[TEST] fill() rejects a quantity greater than remaining\n";

    Order order("AAPL", 10, Side::Buy, OrderType::Limit, 10000, 1, 102);
    order.fill(8);
    expectInvalidArgument(
        [&] { order.fill(3); },
        "fill(3) throws when only 2 shares remain");
    check(order.remaining == 2, "remaining is unchanged when an overfill is rejected");
    check(order.filled() == 8, "filled() is unchanged when an overfill is rejected");
}

void testConstructorValidation() {
    std::cout << "\n[TEST] constructor rejects invalid inputs\n";

    expectInvalidArgument(
        [] { Order("", 1, Side::Buy, OrderType::Limit, 10000, 1, 201); },
        "empty ticker throws std::invalid_argument");
    expectInvalidArgument(
        [] { Order("ABCDEFGH", 1, Side::Buy, OrderType::Limit, 10000, 1, 202); },
        "ticker of 8 characters throws (capacity allows at most 7)");
    expectInvalidArgument(
        [] { Order("AAPL", 0, Side::Buy, OrderType::Limit, 10000, 1, 203); },
        "zero quantity throws std::invalid_argument");
    expectInvalidArgument(
        [] { Order("AAPL", 1, Side::Buy, OrderType::Limit, 0, 1, 204); },
        "zero limit price throws std::invalid_argument");
    expectInvalidArgument(
        [] { Order("AAPL", 1, Side::Sell, OrderType::Limit, -1, 1, 205); },
        "negative limit price throws std::invalid_argument");

    const Order maxLengthTicker(
        "ABCDEFG", 1, Side::Buy, OrderType::Limit, 10000, 1, 206);
    check(maxLengthTicker.tickerView() == "ABCDEFG",
          "ticker with the maximum allowed length (7) is accepted");
}

void testZeroQuantityFillBehavior() {
    std::cout << "\n[TEST] fill(0) behavior\n";

    Order order("AAPL", 10, Side::Buy, OrderType::Limit, 10000, 1, 301);
    check(order.fill(0) == 0, "fill(0) currently returns zero without throwing");
    check(order.remaining == 10, "fill(0) leaves remaining unchanged");
    check(order.filled() == 0, "fill(0) leaves filled() unchanged");
    check(!order.isFilled(), "fill(0) does not mark the order as filled");
}

void testTickerView() {
    std::cout << "\n[TEST] tickerView()\n";

    const Order order("MSFT", 3, Side::Sell, OrderType::Limit, 10001, 2, 401);
    check(order.tickerView() == "MSFT", "tickerView() returns the ticker without null padding");
}

void testLimitOrderFormatting() {
    std::cout << "\n[TEST] limit-order toString() formatting\n";

    Order order("AAPL", 10, Side::Buy, OrderType::Limit, 10000, 42, 501);
    const std::string expectedInitial =
        "Order #501 [client 42] BUY 10 AAPL @ 10000 LIMIT, remaining 10/10";
    check(order.toString() == expectedInitial,
          "limit order formatting includes side, price, type, and initial remaining quantity");

    order.fill(4);
    const std::string expectedPartial =
        "Order #501 [client 42] BUY 10 AAPL @ 10000 LIMIT, remaining 6/10";
    check(order.toString() == expectedPartial,
          "limit order formatting reflects an updated remaining quantity");
}

void testMarketOrderFormatting() {
    std::cout << "\n[TEST] market-order toString() formatting\n";

    Order order("MSFT", 5, Side::Sell, OrderType::Market, 12345, 43, 502);
    check(order.price == 0, "market order stores price zero even if a price argument is supplied");

    const std::string expectedInitial =
        "Order #502 [client 43] SELL 5 MSFT MARKET, remaining 5/5";
    check(order.toString() == expectedInitial,
          "market order formatting omits the limit-price field");

    order.fill(2);
    const std::string expectedPartial =
        "Order #502 [client 43] SELL 5 MSFT MARKET, remaining 3/5";
    check(order.toString() == expectedPartial,
          "market order formatting reflects an updated remaining quantity");
}

} // namespace

int main() {
    std::cout << "Running Order.h unit tests...\n";

    testFilledQuantityAndFillState();
    testOverfillThrowsAndPreservesRemaining();
    testConstructorValidation();
    testZeroQuantityFillBehavior();
    testTickerView();
    testLimitOrderFormatting();
    testMarketOrderFormatting();

    std::cout << "\n========================================\n";
    std::cout << "Checks run: " << checksRun << '\n';
    std::cout << "Checks failed: " << checksFailed << '\n';

    if (checksFailed != 0) {
        std::cout << "[FAIL] Some Order.h tests failed.\n";
        return 1;
    }

    std::cout << "[PASS] All Order.h tests passed.\n";
    return 0;
}

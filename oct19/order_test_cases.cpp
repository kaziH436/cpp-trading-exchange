#include "OrderBook.h"

#include <iostream>

int main() {
    // Pool capacity must leave room for a new incoming order in addition to
    // the orders already resting in the book.
    OrderBook book("AAPL", 32);

    // Existing ask: sell 8 AAPL at price 10001 (for example, $100.01).
    book.submitOrder(Order("AAPL", 8, Side::Sell, OrderType::Limit,
                           10001, 10, 201));

    // This bid cannot match the $100.01 ask, so it rests at 10000.
    book.submitOrder(Order("AAPL", 7, Side::Buy, OrderType::Limit,
                           10000, 11, 202));

    // This bid can match the 8-share ask. It buys 8 and rests its 2-share
    // remainder at price 10001.
    const auto result = book.submitOrder(
        Order("AAPL", 10, Side::Buy, OrderType::Limit, 10001, 12, 203));

    std::cout << "Submit status: ";
    switch (result.status) {
        case OrderBook::SubmitStatus::RestingInBook:
            std::cout << "remainder is resting in the book";
            break;
        case OrderBook::SubmitStatus::FilledCompletely:
            std::cout << "order filled completely";
            break;
        case OrderBook::SubmitStatus::IgnoredWrongTicker:
            std::cout << "wrong ticker; order ignored";
            break;
        case OrderBook::SubmitStatus::DuplicateActiveOrderId:
            std::cout << "duplicate active order ID";
            break;
        case OrderBook::SubmitStatus::PoolFull:
            std::cout << "memory pool is full";
            break;
        case OrderBook::SubmitStatus::MarketRemainderHeld:
            std::cout << "market-order remainder is on hold";
            break;
    }
    std::cout << " (remaining=" << result.remaining << ")\n\n";

    std::cout << book.toString();

    // Example cancellation: remove the 7-share buy order at price 10000.
    std::cout << "\nCanceled order 202: "
              << (book.cancel(202) ? "yes" : "no") << '\n';
    std::cout << book.toString();

    return 0;
}

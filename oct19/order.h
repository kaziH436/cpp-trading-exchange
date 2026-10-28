#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

// make the ticker a string. use delc(type) to make more operator like price < other.price or time < price. 
// minimum code coverage 80%, google test?


// Compile with: -std=c++2c (GCC 14+/Clang 18+) or -std=c++23 if your compiler lacks the flag.

using OrderId  = std::uint64_t;
using ClientId = std::uint64_t;
using Price    = std::int64_t;   // integer ticks (e.g. cents) -- avoid floating point for prices
using Quantity = std::uint32_t;

// "No neighbour" marker for the prev/next links (pool indices).
inline constexpr std::uint32_t kNullIndex = std::numeric_limits<std::uint32_t>::max();

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };

constexpr std::string_view to_string(Side s) {
    return s == Side::Buy ? "BUY" : "SELL";
}

constexpr std::string_view to_string(OrderType t) {
    return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

class Order {
public:
    static constexpr std::size_t kTickerCapacity = 8;

    // Public data members: Order is a plain description of one request.
    // Fixed-size and trivially destructible, so it is cheap to keep in a pool.
    std::array<char, kTickerCapacity> ticker{};  // null-padded, e.g. "AAPL"
    Quantity  size      = 0;                     // quantity originally submitted
    Quantity  remaining = 0;                     // quantity still unfilled
    Side      side      = Side::Buy;
    OrderType type      = OrderType::Limit;
    Price     price     = 0;                     // ignored for market orders
    ClientId  clientId  = 0;
    OrderId   orderId   = 0;

    // Links for the per-price-level doubly linked list (indices into the pool).
    std::uint32_t prev = kNullIndex;
    std::uint32_t next = kNullIndex;

    // Needed so the pool's storage vector can default-construct its slots.
    constexpr Order() = default;

    constexpr Order(std::string_view ticker_, Quantity size_, Side side_, OrderType type_,
                    Price price_, ClientId clientId_, OrderId orderId_)
        : size(size_), remaining(size_), side(side_), type(type_),
          price(type_ == OrderType::Market ? 0 : price_),
          clientId(clientId_), orderId(orderId_) {
        if (ticker_.empty() || ticker_.size() >= kTickerCapacity) {
            throw std::invalid_argument(
                std::format("Order #{}: ticker must be 1 to {} characters, got {}",
                            orderId_, kTickerCapacity - 1, ticker_.size()));
        }
        if (size_ == 0) {
            throw std::invalid_argument(std::format("Order #{}: size must be greater than 0", orderId_));
        }
        if (type_ == OrderType::Limit && price_ <= 0) {
            throw std::invalid_argument(
                std::format("Order #{}: limit order price must be greater than 0, got {}", orderId_, price_));
        }
        std::copy(ticker_.begin(), ticker_.end(), ticker.begin());
    }

    constexpr std::string_view tickerView() const {
        const auto end = std::ranges::find(ticker, '\0');
        return {ticker.data(), static_cast<std::size_t>(end - ticker.begin())};
    }

    constexpr bool isFilled() const { return remaining == 0; }
    constexpr Quantity filled() const { return size - remaining; }

    // Reduces remaining by qty; returns the amount filled. Throws if qty exceeds remaining.
    Quantity fill(Quantity qty) {
        if (qty > remaining) {
            throw std::invalid_argument(
                std::format("Order #{}: cannot fill {} shares, only {} remaining", orderId, qty, remaining));
        }
        remaining -= qty;
        return qty;
    }

    std::string toString() const {
        if (type == OrderType::Market) {
            return std::format("Order #{} [client {}] {} {} {} MARKET, remaining {}/{}",
                               orderId, clientId, to_string(side), size, tickerView(),
                               remaining, size);
        }
        return std::format("Order #{} [client {}] {} {} {} @ {} {}, remaining {}/{}",
                           orderId, clientId, to_string(side), size, tickerView(), price,
                           to_string(type), remaining, size);
    }
};
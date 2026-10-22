#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using OrderId = std::uint64_t;
using ClientId = std::uint64_t;
using Price = std::int64_t;       // Integer price units (for example, cents).
using Quantity = std::uint32_t;

// Sentinel used when an order has no previous or next order in its price queue.
inline constexpr std::uint32_t kNullIndex = std::numeric_limits<std::uint32_t>::max();

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };

constexpr std::string_view to_string(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

constexpr std::string_view to_string(OrderType type) {
    return type == OrderType::Limit ? "LIMIT" : "MARKET";
}

class Order {
public:
    static constexpr std::size_t kTickerCapacity = 8; // Maximum ticker length is 7.

    std::array<char, kTickerCapacity> ticker{}; // Null-padded, e.g. "AAPL".
    Quantity size = 0;                          // Original submitted quantity.
    Quantity remaining = 0;                     // Quantity not yet filled.
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    Price price = 0;                            // Ignored for market orders.
    ClientId clientId = 0;
    OrderId orderId = 0;

    // Indices of adjacent orders at the same price level.
    std::uint32_t prev = kNullIndex;
    std::uint32_t next = kNullIndex;

    constexpr Order() = default;

    constexpr Order(std::string_view ticker_,
                    Quantity size_,
                    Side side_,
                    OrderType type_,
                    Price price_,
                    ClientId clientId_,
                    OrderId orderId_)
        : size(size_),
          remaining(size_),
          side(side_),
          type(type_),
          price(type_ == OrderType::Market ? 0 : price_),
          clientId(clientId_),
          orderId(orderId_) {
        if (ticker_.empty() || ticker_.size() >= kTickerCapacity) {
            throw std::invalid_argument(
                std::format("Order #{}: ticker must contain 1 to {} characters; got {}",
                            orderId_, kTickerCapacity - 1, ticker_.size()));
        }
        if (size_ == 0) {
            throw std::invalid_argument(
                std::format("Order #{}: size must be greater than zero", orderId_));
        }
        if (side_ != Side::Buy && side_ != Side::Sell) {
            throw std::invalid_argument(
                std::format("Order #{}: invalid side value {}", orderId_,
                            static_cast<unsigned>(side_)));
        }
        if (type_ != OrderType::Limit && type_ != OrderType::Market) {
            throw std::invalid_argument(
                std::format("Order #{}: invalid order type value {}", orderId_,
                            static_cast<unsigned>(type_)));
        }
        if (type_ == OrderType::Limit && price_ <= 0) {
            throw std::invalid_argument(
                std::format("Order #{}: limit price must be greater than zero; got {}",
                            orderId_, price_));
        }

        std::copy(ticker_.begin(), ticker_.end(), ticker.begin());
    }

    constexpr std::string_view tickerView() const {
        const auto end = std::ranges::find(ticker, '\0');
        return {ticker.data(), static_cast<std::size_t>(end - ticker.begin())};
    }
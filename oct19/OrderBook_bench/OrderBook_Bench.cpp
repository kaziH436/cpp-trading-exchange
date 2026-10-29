// Benchmark: std::map book vs heap-only book vs heap + hash-table book.
//
// For each "distinct price" setting the SAME pre-generated sequence of orders and cancels is fed to
// all three books (so the work is identical), each run is timed with std::chrono::steady_clock, and
// the median of several repeats is reported. The memory pool, the order-ID hash table and Order
// construction/copying are inside the timed region and are identical for all three versions, so
// the differences come from the price structure only.
//
// Usage: order_bench [ops] [poolCapacity] [repeats]

#include "../heap/OrderBook.h"
#include "../heap_hash/OrderBook.h"
#include "../map/OrderBookMap.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

struct Op {
    bool isCancel;
    OrderId cancelId;  // used when isCancel
    Order order;       // used when !isCancel
};

// Workload shape (cancel is rare, matching is common):
//   5%  cancel of a random recent order id (may already be filled, then cancel returns false)
//   47% aggressive limit order priced across the middle, so it matches resting orders
//   48% passive limit order priced 1..L ticks away from the middle, so it rests
// L = number of distinct prices each side can use.
std::vector<Op> makeWorkload(std::size_t ops, std::size_t distinctPrices, std::size_t pool) {
    std::mt19937_64 rng(20261028);
    constexpr Price kMid = 100000;
    const auto L = static_cast<std::uint64_t>(distinctPrices);
    const std::uint64_t crossDepth = std::max<std::uint64_t>(1, L / 4);

    std::vector<Op> out;
    out.reserve(ops);
    OrderId nextId = 1;
    for (std::size_t i = 0; i < ops; ++i) {
        const auto roll = rng() % 100;
        if (roll < 5 && nextId > 1) {
            const OrderId window = std::min<OrderId>(nextId - 1, pool);
            out.push_back(Op{true, nextId - 1 - (rng() % window), Order{}});
            continue;
        }
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        const Quantity qty = static_cast<Quantity>(1 + rng() % 10);
        Price price;
        if (roll < 52) {  // aggressive: crosses the middle
            const auto depth = static_cast<Price>(rng() % (crossDepth + 1));
            price = side == Side::Buy ? kMid + depth : kMid - depth;
        } else {  // passive: rests away from the middle
            const auto depth = static_cast<Price>(1 + rng() % L);
            price = side == Side::Buy ? kMid - depth : kMid + depth;
        }
        out.push_back(Op{false, 0, Order("AAPL", qty, side, OrderType::Limit, price, 1, nextId++)});
    }
    return out;
}

struct RunResult {
    double milliseconds = 0;
    std::uint64_t filled = 0;     // submits that completed fully
    std::uint64_t rested = 0;     // submits that rested
    std::uint64_t poolFull = 0;   // submits rejected because the pool was full
    std::uint64_t cancelHits = 0;
    std::size_t finalResting = 0;
    std::size_t finalLevels = 0;
};

template <typename Book>
RunResult runOnce(const std::vector<Op>& ops, std::size_t pool) {
    Book book("AAPL", pool);  // building the book (allocating the pool, reserving tables) is not timed
    RunResult r;
    const auto start = std::chrono::steady_clock::now();
    for (const Op& op : ops) {
        if (op.isCancel) {
            r.cancelHits += book.cancel(op.cancelId) ? 1 : 0;
        } else {
            const auto result = book.submitOrder(op.order);
            using Status = typename Book::SubmitStatus;
            if (result.status == Status::FilledCompletely) ++r.filled;
            else if (result.status == Status::RestingInBook) ++r.rested;
            else if (result.status == Status::PoolFull) ++r.poolFull;
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    r.milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();
    r.finalResting = book.restingOrderCount();
    r.finalLevels = book.priceLevelCount(Side::Buy) + book.priceLevelCount(Side::Sell);
    return r;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename Book>
void measure(const std::vector<Op>& ops, std::size_t pool,
             std::vector<double>& times, RunResult& sample) {
    const RunResult r = runOnce<Book>(ops, pool);
    times.push_back(r.milliseconds);
    sample = r;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t ops = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 50000;
    const std::size_t pool = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20000;
    const std::size_t repeats = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 3;

    std::cout << "ops=" << ops << " pool=" << pool << " repeats=" << repeats
              << " (median of repeats; lower is faster)\n\n";
    std::cout << std::left << std::setw(10) << "prices(L)" << std::setw(22) << "version"
              << std::right << std::setw(12) << "median ms" << std::setw(12) << "ns/op"
              << std::setw(10) << "vs map" << std::setw(10) << "filled" << std::setw(10)
              << "rested" << std::setw(10) << "poolFull" << std::setw(11) << "resting"
              << std::setw(9) << "levels" << '\n';

    for (const std::size_t L : {10u, 100u, 1000u, 10000u}) {
        const std::vector<Op> workload = makeWorkload(ops, L, pool);

        std::vector<double> tMap, tHeap, tHash;
        RunResult sMap, sHeap, sHash;
        // Interleave the versions on every repeat so machine noise hits all three equally.
        for (std::size_t rep = 0; rep < repeats; ++rep) {
            measure<OrderBook>(workload, pool, tMap, sMap);
            measure<OrderBookHeap>(workload, pool, tHeap, sHeap);
            measure<OrderBookHeapHash>(workload, pool, tHash, sHash);
        }

        const double mMap = median(tMap), mHeap = median(tHeap), mHash = median(tHash);
        auto row = [&](const char* name, double ms, const RunResult& s) {
            std::cout << std::left << std::setw(10) << L << std::setw(22) << name << std::right
                      << std::fixed << std::setprecision(1) << std::setw(12) << ms
                      << std::setprecision(0) << std::setw(12) << (ms * 1e6 / static_cast<double>(ops))
                      << std::setprecision(2) << std::setw(9) << (ms / mMap) << "x"
                      << std::setw(10) << s.filled << std::setw(10) << s.rested << std::setw(10)
                      << s.poolFull << std::setw(11) << s.finalResting << std::setw(9) << s.finalLevels
                      << '\n';
        };
        row("1 std::map", mMap, sMap);
        row("2 heap only", mHeap, sHeap);
        row("3 heap + hash", mHash, sHash);

        const char* best = "1 std::map";
        double bestMs = mMap;
        if (mHeap < bestMs) { best = "2 heap only"; bestMs = mHeap; }
        if (mHash < bestMs) { best = "3 heap + hash"; bestMs = mHash; }
        std::cout << "          fastest: " << best << "\n\n";
    }
    return 0;
}
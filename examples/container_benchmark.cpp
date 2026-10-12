#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include "common/perf_utils.h"

// ----------------------------------------------------------------------------
// What this benchmark measures
// ----------------------------------------------------------------------------
// We simulate a workload representative of an order book or order ID lookup:
//   - Insert N integer key/value pairs
//   - Look up every key in random order (warm, and with the cache evicted)
//   - Iterate over all elements
//
// All three containers store the same data; only the data structure differs.
// Timings are in raw CPU clock cycles (via RDTSC) so they are independent of
// wall-clock frequency scaling.
//
// Why vectors win for this kind of workload:
//   - std::vector stores elements contiguously in memory.  A sequential scan
//     is a single cache-line stream — the hardware prefetcher handles it
//     almost for free.
//   - std::map is a red-black tree.  Every node is a separate heap allocation
//     scattered across memory.  Each pointer dereference is a potential cache
//     miss (~100–200 cycles on modern hardware vs ~4 cycles for L1 cache).
//   - std::unordered_map avoids tree traversal but still allocates bucket
//     chains on the heap and incurs hashing overhead plus possible cache
//     misses on collision chains.
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
// Sample results
// ----------------------------------------------------------------------------
// Apple M5 (L1d 128 KB, L2 16 MB, 128 B line), built with --config=opt.
// Values are CNTVCT_EL0 ticks (see Common::rdtsc); read them as relative, not
// as literal CPU cycles. The three N tiers walk the memory hierarchy: N=100 is
// L1-resident, N=100K is L2-resident, N=10M spills to DRAM.
//
//   N=100                         vector        map  unordered_map
//     Insert (total)                 125      13542          10417
//     Lookup (avg/op)                 20         22              1
//     Lookup cold (avg/op)           234        703             48
//     Iterate (total)                375        583           1875
//
//   N=100,000                     vector        map  unordered_map
//     Insert (total)              103803    2774517        1192852
//     Lookup (avg/op)                 28         56              1
//     Lookup cold (avg/op)          1004       1486            833
//     Iterate (total)              91261     272324         126754
//
//   N=10,000,000                  vector        map  unordered_map
//     Insert (total)             8116717  432050747      116113301
//     Lookup (avg/op)                143        250              9
//     Lookup cold (avg/op)          1456       2313             68
//     Iterate (total)            9083488   25919216       11369639
//
// Takeaways:
//   - unordered_map wins every lookup (warm and cold) at every size — it touches
//     O(1) cache lines vs O(log N) for the vector's binary search and map's tree.
//   - The DRAM penalty only appears at N=10M: vector lookup 28 -> 143, map 56 ->
//     250 as deep probes start missing all the way to memory. At <=L2 sizes the
//     warm numbers stay flat.
//   - vector dominates insert and iterate at every size (contiguous, no per-node
//     allocation); at 10M its insert is ~53x faster than map.
// ----------------------------------------------------------------------------

// Prevent the compiler from optimizing away reads.
static volatile uint64_t sink = 0;

struct Entry {
    int key;
    int value;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<int> make_keys(std::size_t n) {
    std::vector<int> keys(n);
    std::iota(keys.begin(), keys.end(), 0);
    return keys;
}

static std::vector<int> make_random_keys(const std::vector<int>& keys) {
    std::vector<int> shuffled = keys;
    std::mt19937 rng(42);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    return shuffled;
}

// ---------------------------------------------------------------------------
// Cache eviction (for the "cold" lookup measurement)
// ---------------------------------------------------------------------------
// Buffer larger than this machine's L2 (16 MB) so a full pass evicts L1+L2.
static std::vector<char> g_flush_buffer(64u << 20);  // 64 MB

// Touch one byte per 128-byte cache line (M-series line size) to push the
// container's data out of cache, so the following lookup hits cold memory.
// This work is deliberately performed OUTSIDE the timed region.
static void evict_cache() {
    volatile char s = 0;
    for (std::size_t i = 0; i < g_flush_buffer.size(); i += 128) {
        s += g_flush_buffer[i];
    }
    sink += static_cast<uint64_t>(s);
}

// A full eviction per lookup is expensive, so cold lookups use a small sample
// of keys rather than all N. The keys are already shuffled, so the first K are
// a representative random sample.
static constexpr std::size_t kColdSamples = 256;

// ---------------------------------------------------------------------------
// std::vector<Entry> — sorted so we can use lower_bound for lookup
// ---------------------------------------------------------------------------

static uint64_t bench_vector_insert(std::vector<Entry>& vec, const std::vector<int>& keys) {
    const auto start = Common::rdtsc();
    for (int k : keys) {
        vec.push_back({k, k * 2});
    }
    return Common::rdtsc() - start;
}

static uint64_t bench_vector_lookup(const std::vector<Entry>& vec, const std::vector<int>& lookup_keys) {
    uint64_t total = 0;
    for (int k : lookup_keys) {
        const auto start = Common::rdtsc();
        // Binary search on sorted vector — O(log N), same complexity as map.
        auto it = std::lower_bound(vec.begin(), vec.end(), k,
                                   [](const Entry& e, int v) { return e.key < v; });
        sink += (it != vec.end()) ? it->value : 0;
        total += Common::rdtsc() - start;
    }
    return total / lookup_keys.size();
}

static uint64_t bench_vector_iterate(const std::vector<Entry>& vec) {
    const auto start = Common::rdtsc();
    for (const auto& e : vec) {
        sink += e.value;
    }
    return Common::rdtsc() - start;
}

// ---------------------------------------------------------------------------
// std::map<int,int>
// ---------------------------------------------------------------------------

static uint64_t bench_map_insert(std::map<int, int>& m, const std::vector<int>& keys) {
    const auto start = Common::rdtsc();
    for (int k : keys) {
        m[k] = k * 2;
    }
    return Common::rdtsc() - start;
}

static uint64_t bench_map_lookup(const std::map<int, int>& m, const std::vector<int>& lookup_keys) {
    uint64_t total = 0;
    for (int k : lookup_keys) {
        const auto start = Common::rdtsc();
        auto it = m.find(k);
        sink += (it != m.end()) ? it->second : 0;
        total += Common::rdtsc() - start;
    }
    return total / lookup_keys.size();
}

static uint64_t bench_map_iterate(const std::map<int, int>& m) {
    const auto start = Common::rdtsc();
    for (const auto& [k, v] : m) {
        sink += v;
    }
    return Common::rdtsc() - start;
}

// ---------------------------------------------------------------------------
// std::unordered_map<int,int>
// ---------------------------------------------------------------------------

static uint64_t bench_umap_insert(std::unordered_map<int, int>& m, const std::vector<int>& keys) {
    const auto start = Common::rdtsc();
    for (int k : keys) {
        m[k] = k * 2;
    }
    return Common::rdtsc() - start;
}

static uint64_t bench_umap_lookup(const std::unordered_map<int, int>& m, const std::vector<int>& lookup_keys) {
    uint64_t total = 0;
    for (int k : lookup_keys) {
        const auto start = Common::rdtsc();
        auto it = m.find(k);
        sink += (it != m.end()) ? it->second : 0;
        total += Common::rdtsc() - start;
    }
    return total / lookup_keys.size();
}

static uint64_t bench_umap_iterate(const std::unordered_map<int, int>& m) {
    const auto start = Common::rdtsc();
    for (const auto& [k, v] : m) {
        sink += v;
    }
    return Common::rdtsc() - start;
}

// ---------------------------------------------------------------------------
// Cold lookups — evict the cache before each timed lookup so every probe pays
// the cost of fetching the data structure back from memory.
// ---------------------------------------------------------------------------

static uint64_t bench_vector_lookup_cold(const std::vector<Entry>& vec, const std::vector<int>& lookup_keys) {
    const std::size_t count = std::min(lookup_keys.size(), kColdSamples);
    uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const int k = lookup_keys[i];
        evict_cache();  // not timed
        const auto start = Common::rdtsc();
        auto it = std::lower_bound(vec.begin(), vec.end(), k,
                                   [](const Entry& e, int v) { return e.key < v; });
        sink += (it != vec.end()) ? it->value : 0;
        total += Common::rdtsc() - start;
    }
    return total / count;
}

static uint64_t bench_map_lookup_cold(const std::map<int, int>& m, const std::vector<int>& lookup_keys) {
    const std::size_t count = std::min(lookup_keys.size(), kColdSamples);
    uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        evict_cache();  // not timed
        const auto start = Common::rdtsc();
        auto it = m.find(lookup_keys[i]);
        sink += (it != m.end()) ? it->second : 0;
        total += Common::rdtsc() - start;
    }
    return total / count;
}

static uint64_t bench_umap_lookup_cold(const std::unordered_map<int, int>& m, const std::vector<int>& lookup_keys) {
    const std::size_t count = std::min(lookup_keys.size(), kColdSamples);
    uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        evict_cache();  // not timed
        const auto start = Common::rdtsc();
        auto it = m.find(lookup_keys[i]);
        sink += (it != m.end()) ? it->second : 0;
        total += Common::rdtsc() - start;
    }
    return total / count;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void run_benchmark(std::size_t n) {
    const auto keys        = make_keys(n);
    const auto random_keys = make_random_keys(keys);

    // ---- vector ----
    std::vector<Entry> vec;
    vec.reserve(n);
    const auto vec_insert  = bench_vector_insert(vec, keys);
    // Sort once after bulk insert — common pattern when keys arrive in order.
    std::sort(vec.begin(), vec.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    const auto vec_lookup      = bench_vector_lookup(vec, random_keys);
    const auto vec_lookup_cold = bench_vector_lookup_cold(vec, random_keys);
    const auto vec_iterate     = bench_vector_iterate(vec);

    // ---- map ----
    std::map<int, int> m;
    const auto map_insert      = bench_map_insert(m, keys);
    const auto map_lookup      = bench_map_lookup(m, random_keys);
    const auto map_lookup_cold = bench_map_lookup_cold(m, random_keys);
    const auto map_iterate     = bench_map_iterate(m);

    // ---- unordered_map ----
    std::unordered_map<int, int> um;
    um.reserve(n);  // pre-size to avoid rehashing (gives unordered_map the best chance)
    const auto umap_insert      = bench_umap_insert(um, keys);
    const auto umap_lookup      = bench_umap_lookup(um, random_keys);
    const auto umap_lookup_cold = bench_umap_lookup_cold(um, random_keys);
    const auto umap_iterate     = bench_umap_iterate(um);

    // ---- results ----
    std::cout << "\n=== Container Benchmark (N=" << n << ") ===\n\n";
    std::cout << std::left;

    auto row = [](const char* label, uint64_t vec_cycles, uint64_t map_cycles, uint64_t umap_cycles) {
        printf("  %-28s  vector: %8llu   map: %8llu   unordered_map: %8llu  cycles\n",
               label,
               (unsigned long long)vec_cycles,
               (unsigned long long)map_cycles,
               (unsigned long long)umap_cycles);
    };

    row("Insert (total cycles)",          vec_insert,       map_insert,       umap_insert);
    row("Lookup (avg cycles/op)",         vec_lookup,       map_lookup,       umap_lookup);
    row("Lookup cold (avg cycles/op)",    vec_lookup_cold,  map_lookup_cold,  umap_lookup_cold);
    row("Iterate (total cycles)",         vec_iterate,      map_iterate,      umap_iterate);
}

int main() {
    // Three tiers spanning the memory hierarchy on this machine (L1 128 KB,
    // L2 16 MB):
    //   N=100      -> vector (~0.8 KB) is L1-resident; constant factors dominate.
    //   N=100,000  -> vector (~800 KB) overflows L1 but fits in L2.
    //   N=10,000,000 -> vector (~80 MB) spills L2 into DRAM; locality dominates.
    run_benchmark(100);
    run_benchmark(100'000);
    run_benchmark(10'000'000);

    std::cout << "\nNotes:\n"
              << "  - vector lookup uses binary search (O(log N)), same asymptotic cost as map.\n"
              << "  - vector iteration is a contiguous memory scan — maximally cache-friendly.\n"
              << "  - map nodes are heap-allocated individually; each traversal step risks a cache miss.\n"
              << "  - unordered_map is pre-sized (reserve) to avoid rehashing penalty.\n"
              << "  - 'Lookup cold' evicts the cache (64 MB buffer scan) before each probe so\n"
              << "    the lookup pays the cost of refetching from memory; sampled over "
              << kColdSamples << " keys.\n"
              << "  - at small N, timings are dominated by rdtsc overhead and quantization.\n"
              << "  - sink=" << sink << " (prevents dead-code elimination)\n\n";

    return 0;
}

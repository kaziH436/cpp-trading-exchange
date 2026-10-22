// Using the two design choices provided:
// 1. A single vector to track is_free or in_use: depending on that put the object

// Memory Pool coding
// Eliminate heap allocation at runtime (pre-allocate everything upfront):
// must be fixed size though vector is dynamic memory
// Keep hot objects cache-local in a contiguous vector?
// Avoid any per-allocation overhead

#include <vector>
#include <cstddef>
#include <cstdint>    // CHANGE: Added for std::uint32_t pool indices.
#include <limits>     // CHANGE: Added for the invalid-index marker.
#include <stdexcept>  // CHANGE: Added for get() validation.
#include <new>
#include <utility>

using namespace std;

template <typename T>
class MemPool final {
public:

    // CHANGE: Added an index type matching Order::prev and Order::next.
    using Index = std::uint32_t;

    // CHANGE: Represents an invalid index, equivalent in value to kNullIndex
    // in Order.h. A caller can check this when the pool is full.
    static constexpr Index kInvalidIndex =
        std::numeric_limits<Index>::max();

    explicit MemPool(size_t num_elems)
        : slots(num_elems), in_use(num_elems, false) {}

    // CHANGE: Now returns a pool index instead of T*.
    // Returns kInvalidIndex when no free slot is available.
    // Removed noexcept because constructing T can throw an exception.
    template <typename... Args>
    Index allocate(Args&&... args) {
        for (size_t i = 0; i < in_use.size(); ++i) {
            if (!in_use[i]) {

                void* raw_memory = slots[i].storage;

                T* obj = new (raw_memory)
                    T(std::forward<Args>(args)...);

                // CHANGE: Mark the slot in use AFTER construction succeeds.
                // Otherwise, a constructor exception could leave the slot
                // incorrectly marked as occupied.
                in_use[i] = true;

                // CHANGE: Return the slot index instead of the object pointer.
                return static_cast<Index>(i);
            }
        }

        // CHANGE: Return the invalid-index marker instead of nullptr.
        return kInvalidIndex;
    } // Time complexity O(n)

    // CHANGE: Added indexed access so OrderBook can retrieve an Order
    // using the index stored in PriceLevel or orderIdToIndex.
    T& get(Index index) {
        if (index >= slots.size() || !in_use[index]) {
            throw std::out_of_range(
                "MemPool: invalid index or slot is not in use");
        }

        return *reinterpret_cast<T*>(slots[index].storage);
    }

    // CHANGE: Added const access for read-only operations, such as printing.
    const T& get(Index index) const {
        if (index >= slots.size() || !in_use[index]) {
            throw std::out_of_range(
                "MemPool: invalid index or slot is not in use");
        }

        return *reinterpret_cast<const T*>(slots[index].storage);
    }

    // CHANGE: Now accepts a pool index instead of a T*.
    // OrderBook already has the index when an order is filled or canceled.
    void deallocate(Index index) noexcept {
        // CHANGE: An invalid index or unused slot is a no-op.
        if (index >= slots.size() || !in_use[index]) {
            return;
        }

        // CHANGE: Access the object directly by index, without searching
        // through every slot to find its memory address.
        reinterpret_cast<T*>(slots[index].storage)->~T();

        in_use[index] = false;
    } // Time complexity O(1)

    MemPool() = delete;
    MemPool(const MemPool&) = delete;
    MemPool(MemPool&&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    MemPool& operator=(MemPool&&) = delete;

private:
    struct Slot {
        // Stores bytes for one T and ensures appropriate alignment.
        alignas(T) byte storage[sizeof(T)];
    };

    vector<Slot> slots;
    vector<bool> in_use;
};
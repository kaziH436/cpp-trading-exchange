// Using the two design choices provded:
// 1. a single vector to track is_free or in_use: depending on that put the object
// 2. two vectors: one for is_free or in_use vector and one for object vector for adding/removing

// 1. What are the difference between the two design choices e.g. time complexity or space complexity 
// 2. Code the memory pool implementation using both the design choices 
// 3. Add test cases to see how the two design choices compare

// Memory Pool coding 
// Eliminate heap allocation at runtime (pre-allocate everything upfront)​: must be fixed size though vector is dynamic memory
// Keep hot objects cache-local in a contiguous vector​
// Avoid any per-allocation overhead​



#include <vector>
#include <cstddef>
#include <new>
#include <utility>
using namespace std;

template <typename T> 
class MemPool final {
public: 

    // constructor
    explicit MemPool(size_t num_elems): slots(num_elems), in_use(num_elems, 0) {}
    // in_use is intialized with 0
        

    template <typename...Args> 
    
    T *allocate(Args &&...args) noexcept{ 
        for (size_t i = 0; i < in_use.size(); ++i) { // iterate through vector 1 (status) to check if vector 2 (holding objects) has a free space
            if (!in_use[i]) { // if a slot is not in use 
                in_use[i]=1; // make use of it 
                void *raw_memory = slots[i].storage; // address of slot i's bytes
                
                // placement new 
                T *obj = new (raw_memory) T(args...); //build the T object
                return obj; 
            }

        }
        return nullptr; // no free slot 
    } 
    // noexcept is a promise about behavoir, the function won't let an exception pass and it will crash first if there is an exception 


    void deallocate(const T *elem) noexcept{ 
        if (!elem) return;
        auto index = reinterpret_cast<const Slot *>(elem) - slots.data(); // slots.data start of the bytes address
        elem->~T(); // freed up space
        in_use[index] = 0;

    }

    
    MemPool()=delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &&) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &&) = delete("a MemPool owns its backing storage and free list");

    private:
        struct Slot { 
            alignas(T) byte storage[sizeof(T)];
        };
        vector<Slot> slots; 
        vector<char> in_use;  // vector<bool> is not good implementation as it doesn't stores bool values 
}; 
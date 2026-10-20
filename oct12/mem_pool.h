// Using the two design choices provded:
// 1. a single vector to track is_free or in_use: depending on that put the object

// Memory Pool coding 
// Eliminate heap allocation at runtime (pre-allocate everything upfront)​: must be fixed size though vector is dynamic memory
// Keep hot objects cache-local in a contiguous vector​? 
// Avoid any per-allocation overhead​


// add the error messages, throw an exception using try except 
#include <vector>
#include <cstddef>
#include <new>
#include <utility>
using namespace std;

template <typename T> 
class MemPool final {
public: 

    // constructor
    explicit MemPool(size_t num_elems): slots(num_elems), in_use(num_elems, false) {}
    // in_use is intialized with 0
        

    template <typename...Args> 
    
    T *allocate(Args &&...args) noexcept{ 
        for (size_t i = 0; i < in_use.size(); ++i) { // iterate through vector 1 (status) to check if vector 2 (holding objects) has a free space
            if (!in_use[i]) { // if a slot is not in use 
                in_use[i]=true; // make use of it 
                void *raw_memory = slots[i].storage; // address of slot i's 
                
                // placement new 
                T* obj = new (raw_memory)
                        T(std::forward<Args>(args)...); //forward helps take both r and lvalues
                return obj;     
            }

        }
        return nullptr; // no free slot 
    } // time complexity O(n)

    // noexcept is a promise about behavoir, the function won't let an exception pass and it will crash first if there is an exception 


    void deallocate(const T* elem) noexcept {
        if (!elem) return; // if nullptr, return

        for (size_t i = 0; i < slots.size(); ++i) {
            
            // compares the memory addresses of elem and i; ensures the pointers are of same type
            if (static_cast<const void*>(elem) == static_cast<const void*>(slots[i].storage)) {

                if (!in_use[i]) return; // if not in use, return 

                // removes const qualification from the pointer to deconstruct the object T
                const_cast<T*>(elem)->~T(); 
                in_use[i] = false;
                return;
            }
        }
    } // time complexity O(n)

    
    MemPool()=delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &&) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &&) = delete("a MemPool owns its backing storage and free list");

    private:
        struct Slot { 
            //stores bytes for 1 T and ensures its aligned since string or int will be aligned differently
            alignas(T) byte storage[sizeof(T)]; 
        };
        vector<Slot> slots; 
        vector<bool> in_use;  // vector<bool> is not good implementation as it doesn't stores bool values 
}; 
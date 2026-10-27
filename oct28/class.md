# Order book: data structure design in C++

# Quiz

# std::format is generally preferred over repeated + concatenation to create strings 

# std::map is sorted by key and in kind of a binary tree structure 
# O(log n) for find and insert
# O(log n) for remove 

# average case lookup in (maybe multi map)is O(1) 
# and worst case is O(n): True, O(n) if the key has 
# multiple values then removing the value -> no point in hashing? 

# std::unordered_map, lookup average case is O(1) since key is there 
# but worst case 

# Any struct that can be stored in std::map and std::unordered_map
# map orders by key 
# to put a struct in an ordered map: the ordering has to be defined 
# but unordered_map: it needs to know how to hash? how does the equality operator and hash function works 




# perioity queue
underlying structure is an array but the idea in indexing through is 2i and 2i+1


data structures must be picked based on what operations happens the most


O(log n) to find the best bid/ask: using key as price

using two priority queue: 
bid if 90 (buyer): I want 90 or less  maxheap
ask if 90 (seller): I want 90 or more minheap

O(n) for finding a particular price? maybe using a deleted 
- 1. one way to handle is by flagging it and skipping it if it does get matched
- 2. 
 


## Vectors
This is not a good structure as finding the best/removing is O(n)
not sure if this would be the case is the vector is sorted 

## Sorted Map
finding the best price: O(log n) but maybe using a pointer to the best price 
but rebalancing the tree is O(n)

how would the orders be perserved? the tree needs to be restructed 


## 


# order book: more features 
- update (orderID): down-sizing keeps queue priority? 
    - while up-sizing puts order at the end? 

- Disconeect(clientID)
    - cancel on disconnect

- order (ttl):
    - cancelled after two minutes 

- self-trade prevention: decrement and cancel
    - if someone does ask and bid at the same time, make sure to not match themselves 
    - 
    - 


# Google test (gtest)
- expect: records the failure and keeps running 
- assert: fails if the test is false
    - use this when contiuing would crash or be meaningless, e.g. asset_ne(ptr, nullptr) becore derefercing 
- bazel converage for test coverage report: coverage is terrible metric because covering code lines doesn't mean it covers all behaviours of the code
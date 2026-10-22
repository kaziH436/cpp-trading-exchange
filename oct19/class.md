# Quiz 1

## Containers

1. `std::array` and `std::vector` provide O(1) for random 
   - **True**

2. Erasing an element from the middle will invalidate all the elements
   - **False**

3. Vectors and arrays will be shifting the elements
   - **Vector:** faster for inserting in the middle (5-10x faster than iterating through the list)
   - **List:** faster for insertion and deletion
   - If a pointer points to memory, that memory may not be loaded in the cache.

4. **True.** Inserting at the end of an array is O(n) because it needs to be iterated through to get to the end, but the linked list has a tail pointer.
   - Array insert can't work in C++ as it's fixed size.

## Templates

- `template<typename T>`: only works when it's the same dtype and the condition is correct

## E / F

- **E:**
- **F:**

## Memory pool design choices

Difference between the 2 memory pool design choices:

- The 2nd memory pool is implemented using a tuple data structure
- Finding smth
- Using the boolean will be faster since it's smaller than the 2nd vector, but the first vector and second vector will be allocated right

## Hash Maps vs. Trees

- `std::unordered_map` (lookup table)
  - insert, find, erase
- `std::map` (a tree?)
  - O(log n) for insert, find, erase
- Is a std::map stored as an array? No. A heap can live in an array because it’s a complete tree: every level is filled left to right, so a node’s children are found by index math (children of position i sit at 2i+1 and 2i+2). A std::map is a red-black tree where every node is allocated separately and holds pointers to its parent, left child, and right child. The nodes can be scattered anywhere in memory. So it’s not true that every tree is an array, and not everything is consecutive memory. Linked lists, maps, and the node storage of unordered_maps are all pointer-based. Vectors and heaps are the contiguous ones.

**Ask:** Keep hot objects cache-local in a contiguous vector?

---

## Trading Exchange

- Stock exchange, commodities, crypto

### Matching Engine

- Central limit order book: maintains all active buy/sell orders
- Matches new orders with an existing order or adds it to the order book

### Order Book

- **Asks:** orders to sell
- **Bids:** orders to buy

### Matching Algorithm

Order info:

- **Ticker** (e.g. AAPL)
- **Price** of 1 share of the stock (or is it based on the total number of shares?)
- **Size** (number of shares)
- **Side** (e.g. buy or sell)
- **Order types:**
  - **Limit order:** buy at this price `>=`
  - **Market order:** buy at whatever price,
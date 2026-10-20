## Quiz 1

1. std::array and std::vector provides O(1) for random 
True

2. Erasing an element from the middle will invalidate all the elements
False 

3. Vectors and array will be shifting the elements


Vector: faster for inserting in the middle (5-10x faster than iterating through the list)
List: faster for insertion and deletion 

if a pointer points to memory, that memory may not be loaded in the cache. 

4. True, inserting an the end of an array is O(n) as it needs to be iterated through to get to the end but the linked list, there is the tail pointer
- array insert can't work in c++  as its fixed size 

template<typename T>: only works when its same dtype and condition correct 

E:

F: 

Difference between the 2 memory pool design choices 
- the 2nd memory pool is implemented using tuple data structure
- Finding smth
- using the boolean will be faster since its smaller than the 2nd vector, but the first vector and second vector will be allocated right


std::unordered_map (lookup table)
- insert, find, erase 

std::map (a tree?)
- O(log n) for insert, find, erase



ask // Keep hot objects cache-local in a contiguous vector​? 


trading exchange:
- stock exchange, commidities, cypto

Matching matching engine:
- central limit order book: maintains all active buy/sell orders 
- matches new orders with an existing order or adds it to an order book

Order book
- asks (placing orders to sell)

- bids (placing orders to buy)

Matching algo:
order info:
- ticker (e.g. AAPL)
- price of 1 share of the stock ( or is it based on the total number of shares )
- size (number of shares) 
- side (e.g. buy or sell)
- order types: 
    - limit order: buy at this price >=
    - market order buy at whatever price, hence matches with the best ask
- client Id 
- order ID 

Order Book transations 
100.01 -> B

Hw2 : 
implement <order>

hw1: 
implement design 2 
design1; fix the loop using bool
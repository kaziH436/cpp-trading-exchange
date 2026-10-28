# Order Book: Data Structure Design in C++

## 1. Quiz: C++ Data Structures and Complexity

### `std::format`

- `std::format` is generally preferred over repeated string concatenation with `+` when constructing formatted strings because it improves readability and separates formatting from values.

### `std::map`

- Stores key-value pairs sorted by key.
- Typically implemented as a self-balancing binary search tree.
- Operations:
  - **Find:** `O(log n)`
  - **Insert:** `O(log n)`
  - **Remove:** `O(log n)`
- The ordering of keys must be defined, either through the key type's comparison operator or a custom comparator.

### `std::unordered_map`

- Stores key-value pairs using a hash table.
- Operations:
  - **Lookup:** Average `O(1)`, worst-case `O(n)`.
  - **Insert:** Average `O(1)`, worst-case `O(n)`.
  - **Remove:** Average `O(1)`, worst-case `O(n)`.
- Worst-case complexity can occur when many keys collide in the same hash bucket.

### Custom Structs in Maps

- **`std::map`:** Requires a way to compare keys and establish an ordering.
- **`std::unordered_map`:** Requires a hash function and an equality comparison to determine whether two keys are equivalent.
- Hashing helps locate a bucket; equality comparison distinguishes keys within that bucket.

### Average-Case Lookup and Multiple Values

- A hash table can still provide average `O(1)` lookup when multiple keys collide, assuming a reasonable hash distribution.
- If a key maps to multiple values, the complexity of finding or removing a particular value depends on the data structure used to store those values.

---

## 2. Priority Queues

- A priority queue is commonly implemented using a **binary heap**.
- The heap is typically stored in an array.
- For a zero-indexed array, the children of the element at index `i` are:
  - Left child: `2i + 1`
  - Right child: `2i + 2`
  - Parent: `(i - 1) / 2`, for `i > 0` using integer division.

### Using Priority Queues for an Order Book

- **Bids (buyers):** Use a max-heap to prioritize the highest bid.
- **Asks (sellers):** Use a min-heap to prioritize the lowest ask.

Example:
- A buyer bidding 90 wants to buy at a price of 90 or less.
- A seller asking 90 wants to sell at a price of 90 or more.
- The order book matches compatible bids and asks according to its matching rules.

### Priority Queue Complexity

| Operation | Typical Complexity |
|---|---|
| View best bid/ask | `O(1)` |
| Insert order | `O(log n)` |
| Remove top order | `O(log n)` |
| Find an arbitrary order | `O(n)` |

### Handling Cancelled Orders in a Priority Queue

1. **Lazy deletion:** Mark an order as cancelled and skip it when it reaches the top of the heap.
2. **Indexed heap:** Maintain a mapping from order ID to heap index so an arbitrary order can be located and removed efficiently.

Lazy deletion is simpler to implement, but cancelled orders may remain in the heap until they reach the top.

---

## 3. Choosing a Data Structure for an Order Book

**General principle:** Choose data structures based on which operations occur most frequently and which performance guarantees are required.

### Vectors

- Finding the best price in an unsorted vector takes `O(n)`.
- Removing an arbitrary element takes `O(n)` because subsequent elements may need to shift.
- Inserting an element at the end is amortized `O(1)`.

**What if the vector is sorted?**

- Finding the best price can be `O(1)` if the best price is kept at a known end.
- Binary search can find a particular price in `O(log n)`.
- Inserting or removing an element in the middle is still `O(n)` because elements must shift.

### Sorted Map: `std::map`

- Store price levels as keys.
- Store the orders at each price level as the associated value.
- The lowest ask is available at `begin()` when asks are stored in ascending price order.
- The highest bid is available at `rbegin()` when bids are stored in ascending price order.
- Finding a specific price level takes `O(log n)`.
- Inserting or removing a price level takes `O(log n)`.

**What about tree rebalancing?**

- A self-balancing tree may need rotations after insertions and removals.
- These operations are included in the `O(log n)` complexity.
- Rebalancing does not generally require `O(n)` work.

**How is order priority preserved?**

- Maintain a separate FIFO queue of orders at each price level.
- New orders at the same price are appended to the end of that queue.
- Orders are matched from the front of the queue.
- Rebalancing the price-level tree does not change the FIFO order of orders stored within each price level.

### Two Priority Queues

- Use a max-heap for bids and a min-heap for asks.
- Retrieving the best price is `O(1)`.
- Insertion and removal of the top order are `O(log n)`.
- Finding or removing an arbitrary order is `O(n)` without additional indexing.
- Maintaining FIFO priority among orders at the same price and supporting cancellation can require extra bookkeeping.

### Key Trade-Off

| Data Structure | Best Price | Insert | Arbitrary Order Lookup |
|---|---:|---:|---:|
| Unsorted vector | `O(n)` | Amortized `O(1)` at end | `O(n)` |
| Sorted vector | `O(1)` at a known end | `O(n)` in general | `O(log n)` by price |
| `std::map` of price levels | `O(log n)` to locate a new best level; `O(1)` to access an iterator to the current best | `O(log n)` per price-level insertion | `O(n)` unless indexed by order ID |
| Priority queue | `O(1)` | `O(log n)` | `O(n)` without indexing |

*Note: Complexity depends on whether the operation concerns a price level or an individual order.*

---

## 4. Order Book: Additional Features

### Update an Order by `orderID`

- **Downsizing:** Reducing the remaining quantity may preserve queue priority, depending on the exchange's rules.
- **Upsizing:** Increasing the quantity may cause the order to lose priority and move to the end of the queue.
- The exact behavior depends on the exchange's matching rules.

### Disconnect by `clientID`

- Cancel all open orders associated with the disconnected client.
- Prevent disconnected clients from leaving unintended active orders in the book.

### Time-to-Live (TTL)

- Give an order an expiration time.
- Example: An order is cancelled automatically after two minutes.
- Expired orders must be removed or marked inactive so they cannot be matched.

### Self-Trade Prevention

- Prevent a client's own buy and sell orders from matching against each other.
- Example: If the same client has a bid and an ask that would cross, the exchange applies its configured self-trade prevention policy.
- Possible policies include:
  - **Cancel newest:** Cancel the incoming order.
  - **Cancel oldest:** Cancel the resting order.
  - **Decrement and cancel:** Reduce the quantities and cancel the relevant remaining quantity according to the exchange's rules.

---

## 5. Google Test (GTest)

### `EXPECT_*` vs. `ASSERT_*`

**`EXPECT_*`**
- Records a test failure but continues executing the current test.
- Use when subsequent checks can still run meaningfully after a failure.

**`ASSERT_*`**
- Records a fatal failure and stops the current test function.
- Use when continuing would cause a crash or make the remaining checks meaningless.

Example:

```cpp
ASSERT_NE(ptr, nullptr);
EXPECT_EQ(ptr->value, expected_value);
```

- `ASSERT_NE` ensures the pointer is not null before dereferencing it.
- If the pointer is null, the test stops safely instead of attempting to access invalid memory.

### Test Coverage with Bazel

- Bazel can generate test coverage reports.
- Coverage helps identify code that tests do not execute.
- However, **high line coverage does not guarantee correctness**.

For example:
- A test might execute every line of a function without checking important edge cases.
- A function might be tested with valid inputs but not invalid inputs.
- Tests might miss incorrect state transitions, ordering bugs, or boundary conditions.

**Key takeaway:** Coverage is useful for identifying untested code, but comprehensive testing must also verify behavior, edge cases, failure conditions, and state transitions.

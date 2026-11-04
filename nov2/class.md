# Optimizing the Order Book

## Quiz

**Question:** When implementing an order book, what is the trade-off between using a priority queue versus a sorted map to store orders?

### Advantages

**Priority Queue**
- Uses a min-heap and max-heap to efficiently match orders.
- Cache-friendly.
- Insertion: `O(log n)`.
- Pop: `O(log n)`.

**Sorted Map**
- Maintains orders in sorted order by price.
- Potentially more efficient for certain operations, such as cancellation.

### Disadvantages

**Cancellation**
- Priority queue: potentially `O(log n)` or requires additional indexing/lazy deletion.
- Sorted map: `O(1)` in certain cases, depending on how the order is referenced and stored.

**Current Implementation**
- The current implementation does not optimize for the specific workload.

---

## Fast Limit Order Book

### Key Observations

- Optimize for the particular workflow rather than using a general-purpose data structure.
- Instead of spreading prices across a large range of values
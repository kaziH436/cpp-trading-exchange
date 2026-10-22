// OrderBook: core component of an exchange matching engine.
// Keeps track of the CLOB (central limit order book):
//   - maintains all active (resting) buy/sell orders
//   - matches a new order against existing orders on the opposite side,
//     then rests any leftover (limit orders) in the book

class OrderBook {

    // ---------- data structures ----------

    // buys:  map<price, PriceLevel>, highest price first (comparator flipped)
    // sells: map<price, PriceLevel>, lowest price first  (default comparator)
    //   key   = price (the sorted tree searches/sorts by it)
    //   value = PriceLevel: pool index of the first (head) and last (tail) order at that price
    //     - the queue is a doubly linked list, no separate node class:
    //       the orders in the pool are the nodes (Order.prev / Order.next, kNullIndex = none)
    //     - new orders go at the tail, matching takes from the head (FIFO within a price)
    //   best price = first element of the map (begin()), NOT the root
    //     buys.begin() = highest bid, sells.begin() = lowest ask

    // unordered_map<orderID, pool index>   // for cancel; an index, not a copy of the order

    // memory pool: owns the Order objects; maps, lists, and the ID map only hold indices
    // pool Boolean: true = slot in use, false = slot free


    // ---------- matching (runs first when a new order arrives) ----------

    match(incoming):
        opposite = (incoming is buy) ? sells : buys

        while incoming.remaining > 0 and opposite is not empty:
            best = opposite.begin()

            if incoming is a limit order and best price fails the price check: break
                // buy:  best ask price <= incoming.price
                // sell: best bid price >= incoming.price
                // market order: always passes

            resting = head order of best's linked list
            traded  = min(incoming.remaining, resting.remaining)
            incoming.fill(traded)
            resting.fill(traded)

            if resting is fully filled:
                remove resting from the head of the linked list
                erase resting's orderID from the unordered_map
                free resting's pool slot (Boolean -> false)
                if the linked list is now empty: erase that price key from opposite
            // a partially filled resting order stays at the head of its queue

        // after the loop:
        if incoming.remaining == 0:
            free incoming's pool slot (Boolean -> false)
            // never added to the unordered_map
        else if incoming is a limit order:
            rest(incoming)
        else:
            // market order with unfilled leftover: ON HOLD


    // ---------- resting a limit order ----------

    rest(order):
        map = (order is buy) ? buys : sells     // its OWN side's map
        try_emplace(order.price) into map       // creates the level if missing, finds it if it exists
        append order to the tail of that level's linked list
        unordered_map[order.orderID] = order's pool index


    // ---------- cancel ----------

    cancel(orderID):
        find orderID in the unordered_map -> pool index (not found: nothing to cancel)
        read the order's side and price from the pool
        remove the order from its linked list (connect prev and next to each other;
                                               update head/tail if it was first or last)
        if the linked list is now empty: erase that price key from buys/sells
        erase orderID from the unordered_map
        free the pool slot (Boolean -> false)


    // add(order)   // ON HOLD: it came from the professor, to be looked at later

    // toString(): print each side's price levels, best price first,
    //             with each level's orders in queue order
}
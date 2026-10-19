#include <cassert>
#include <iostream>
#include "mem_pool.h"

using namespace std;

struct Tracked {
    static int alive;
    int id;

    explicit Tracked(int new_id) : id(new_id) {
        ++alive;
    }

    ~Tracked() {
        --alive;
    }
};

int Tracked::alive = 0;

// Tests whether the pool keeps storage without constructing objects
void test_construction() {
    cout << "Test 1: Pool construction\n";

    Tracked::alive = 0;
    MemPool<Tracked> pool(3);

    assert(Tracked::alive == 0);

    cout << "Passed: No objects constructed initially.\n\n";
}

// Tests object construction and destruction.
void test_allocate_and_deallocate() {
    cout << "Test 2: Allocation and deallocation\n";

    Tracked::alive = 0;
    MemPool<Tracked> pool(2);

    Tracked* obj = pool.allocate(42);

    assert(obj != nullptr);
    assert(obj->id == 42);
    assert(Tracked::alive == 1);

    pool.deallocate(obj);

    assert(Tracked::alive == 0);

    cout << "Passed: Objects are constructed and destroyed correctly.\n\n";
}

// Tests whether multiple constructor arguments work.
struct Pair {
    int a;
    double b;

    Pair(int first, double second) : a(first), b(second) {}
};

void test_multiple_arguments() {
    cout << "Test 3: Multiple constructor arguments\n";

    MemPool<Pair> pool(2);

    Pair* obj = pool.allocate(7, 9.5);

    assert(obj != nullptr);
    assert(obj->a == 7);
    assert(obj->b == 9.5);

    pool.deallocate(obj);

    cout << "Passed: Multiple arguments forwarded correctly.\n\n";
}

// Tests whether simultaneous allocations have different addresses.
void test_different_addresses() {
    cout << "Test 4: Different allocation addresses\n";

    Tracked::alive = 0;
    MemPool<Tracked> pool(3);

    Tracked* first = pool.allocate(1);
    Tracked* second = pool.allocate(2);
    Tracked* third = pool.allocate(3);

    assert(first != nullptr);
    assert(second != nullptr);
    assert(third != nullptr);

    assert(first != second);
    assert(first != third);
    assert(second != third);

    pool.deallocate(first);
    pool.deallocate(second);
    pool.deallocate(third);

    assert(Tracked::alive == 0);

    cout << "Passed: Allocated objects have different addresses.\n\n";
}

// Tests what happens when the pool runs out of space.
void test_full_pool() {
    cout << "Test 5: Pool capacity\n";

    Tracked::alive = 0;
    MemPool<Tracked> pool(2);

    Tracked* first = pool.allocate(1);
    Tracked* second = pool.allocate(2);
    Tracked* third = pool.allocate(3);

    assert(first != nullptr);
    assert(second != nullptr);
    assert(third == nullptr);

    assert(Tracked::alive == 2);

    pool.deallocate(first);
    pool.deallocate(second);

    assert(Tracked::alive == 0);

    cout << "Passed: Full pool returns nullptr.\n\n";
}

// Tests whether a deallocated slot can be reused.
void test_slot_reuse() {
    cout << "Test 6: Slot reuse\n";

    Tracked::alive = 0;
    MemPool<Tracked> pool(2);

    Tracked* first = pool.allocate(1);
    Tracked* second = pool.allocate(2);

    assert(first != nullptr);
    assert(second != nullptr);

    pool.deallocate(first);

    Tracked* replacement = pool.allocate(99);

    assert(replacement != nullptr);
    assert(replacement == first);
    assert(replacement->id == 99);
    assert(second->id == 2);

    pool.deallocate(replacement);
    pool.deallocate(second);

    assert(Tracked::alive == 0);

    cout << "Passed: Deallocated slot can be reused.\n\n";
}

// Tests whether passing nullptr is harmless.
void test_nullptr() {
    cout << "Test 7: Null pointer deallocation\n";

    MemPool<int> pool(2);

    pool.deallocate(nullptr);

    cout << "Passed: nullptr is handled safely.\n\n";
}

int main() {
    test_construction();
    test_allocate_and_deallocate();
    test_multiple_arguments();
    test_different_addresses();
    test_full_pool();
    test_slot_reuse();
    test_nullptr();

    cout << "All memory pool tests passed!\n";

    return 0;
}
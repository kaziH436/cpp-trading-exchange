#include <cassert>    
#include <iostream>   
#include <vector>
#include "mem_pool.h"
using namespace std;

// A test object that counts how many of itself currently exist.
struct Tracked {
    static int alive;   // shared counter for ALL Tracked objects
    int id;             // value we store so tests can check it

    Tracked(int new_id) {   // needs an id, so no zero-argument version exists
        id = new_id;
        alive = alive + 1;  // one more object exists
    }

    ~Tracked() {
        alive = alive - 1;  // one fewer object exists
    }
};
int Tracked::alive = 0;     // counter starts at zero

// A test object with two values, to check multiple arguments work.
struct Pair {
    int a;
    double b;

    Pair(int first, double second) {
        a = first;
        b = second;
    }
};

void test_construction_creates_no_objects() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(4);
    assert(Tracked::alive == 0);   // only raw space exists, no objects yet
}

void test_allocate_builds_object_and_deallocate_destroys_it() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(4);

    Tracked *t = pool.allocate(42);
    assert(t != nullptr);          // allocation succeeded
    assert(t->id == 42);           // object was built with our value
    assert(Tracked::alive == 1);   // one object exists

    pool.deallocate(t);
    assert(Tracked::alive == 0);   // destructor ran
}

void test_multiple_constructor_arguments() {
    MemPool<Pair> pool(2);

    Pair *p = pool.allocate(7, 9.5);
    assert(p != nullptr);
    assert(p->a == 7);
    assert(p->b == 9.5);

    pool.deallocate(p);
}

void test_allocations_get_different_addresses() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(3);

    Tracked *first = pool.allocate(1);
    Tracked *second = pool.allocate(2);
    Tracked *third = pool.allocate(3);

    assert(first != second);
    assert(second != third);
    assert(first != third);
    assert(first->id == 1);
    assert(second->id == 2);
    assert(third->id == 3);
}

void test_full_pool_returns_nullptr() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(2);

    Tracked *first = pool.allocate(1);
    Tracked *second = pool.allocate(2);
    Tracked *extra = pool.allocate(3);   // no room left

    assert(extra == nullptr);
    assert(first->id == 1);              // existing objects untouched
    assert(second->id == 2);
    assert(Tracked::alive == 2);         // no extra object was built
}

void test_freed_slot_can_be_reused() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(2);

    Tracked *first = pool.allocate(1);
    Tracked *second = pool.allocate(2);
    assert(pool.allocate(3) == nullptr);   // full

    pool.deallocate(first);
    Tracked *reused = pool.allocate(9);    // should work now
    assert(reused != nullptr);
    assert(reused->id == 9);
    assert(second->id == 2);               // untouched
}

void test_freeing_middle_slot_frees_the_right_one() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(3);

    Tracked *first = pool.allocate(1);
    Tracked *middle = pool.allocate(2);
    Tracked *last = pool.allocate(3);

    pool.deallocate(middle);
    Tracked *replacement = pool.allocate(4);

    assert(replacement == middle);   // the freed slot is the one reused
    assert(first->id == 1);
    assert(last->id == 3);
}

void test_deallocate_nullptr_is_harmless() {
    MemPool<Tracked> pool(1);
    pool.deallocate(nullptr);   // must not crash
}

void test_slots_are_evenly_spaced() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(3);

    Tracked *first = pool.allocate(1);
    Tracked *second = pool.allocate(2);
    Tracked *third = pool.allocate(3);

    // pointer subtraction counts objects between addresses
    assert(second - first == third - second);
    assert(second - first > 0);
}

void test_free_everything() {
    Tracked::alive = 0;
    MemPool<Tracked> pool(100);
    vector<Tracked *> all_pointers;

    for (int i = 0; i < 100; ++i) {
        all_pointers.push_back(pool.allocate(i));
    }
    assert(Tracked::alive == 100);

    for (Tracked *p : all_pointers) {
        pool.deallocate(p);
    }
    assert(Tracked::alive == 0);
}

int main() {
    test_construction_creates_no_objects();
    test_allocate_builds_object_and_deallocate_destroys_it();
    test_multiple_constructor_arguments();
    test_allocations_get_different_addresses();
    test_full_pool_returns_nullptr();
    test_freed_slot_can_be_reused();
    test_freeing_middle_slot_frees_the_right_one();
    test_deallocate_nullptr_is_harmless();
    test_slots_are_evenly_spaced();
    test_free_everything();

    cout << "All tests passed\n";
    return 0;
}
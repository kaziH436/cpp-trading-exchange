# October 12 Class: Quiz

## Log Function

A log function needs to print a string to the console without making unnecessary copies of the string.

```cpp
log(user.getName());
log("hello");
```

```cpp
// Makes a copy, which is inefficient
void log(std::string s)

// Makes a reference to the string, no copy is made, more efficient,
// but it won't work as is: it is passing an r-value (non-const reference)
void log(std::string& s)

// Makes a reference to the string, no copy is made and the r-value won't change
void log(const std::string& s)

// Makes a pointer to the string, and it won't work
void log(std::string* s)
```

```cpp
int x = 5;
```

- **R-values** are temporary.
- **L-values** are variables that have a name and an address in memory.

## Memory

- Creating a class falls into main memory.
- Going down to the stack means latency increases.
- **Cache** is a fast memory, a subset of main memory, that makes latency better.
- **Locality:** if the data is near, the cache is faster.
  - **Temporal locality:** if the data was accessed recently, it is likely to be accessed again.
  - **Spatial locality:** if the data is accessed, nearby data is likely to be accessed.

## Data Structures

### Stack

1. Stored in memory like a contiguous array that is fixed size.
2. LIFO (last in, first out).

```cpp
int main() {

    return 0;
}
```

## C++ Containers

### `std::array`, `std::vector`

```cpp
std::array<int, 5>

std::vector<int>
```

- Time to access an element is O(1).

### `std::list<int>`

- Kind of like a doubly linked list.
- O(1) insert/remove.
- No random access means?

### `std::deque`

- Fixed-size blocks indexed by a map of pointers.
- `push_front`, `push_back` are O(1).
- Random access possible?
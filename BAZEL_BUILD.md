# Bazel Build System

This project uses **Bazel**, a modern, scalable build system developed by Google.

## Installation

### macOS (via Homebrew)
```bash
brew install bazel
```

### Linux
```bash
# Ubuntu/Debian
sudo apt-get install bazel
```

## Building

```bash
bazel build //...          # build everything
bazel build //examples/... # build examples only
```

## Running

```bash
bazel run //examples:container_benchmark
```

## Project Structure

```
cpp-trading-exchange/
├── MODULE.bazel           # Bazel dependency declaration
├── .bazelrc               # Bazel settings
├── common/
│   └── perf_utils.h       # RDTSC cycle counter utility
├── examples/
│   └── container_benchmark.cpp
```

## Build Targets

| Target | Description |
|--------|-------------|
| `//common:perf_utils` | RDTSC cycle counter header library |
| `//examples:container_benchmark` | Benchmarks vector vs map vs unordered_map |

## Compiler Flags

All targets compile with:
- **C++ Standard**: C++26 (`-std=c++26`)
- **Warnings**: `-Wall -Wextra -Werror -Wpedantic` (strict)

## Build Configurations

```bash
bazel build //...                   # fastbuild (default, fastest iteration)
bazel build --config=opt //...      # optimised (-O2)
bazel build --config=dbg //...      # debug symbols (-O0 -g)
bazel build --config=asan //...     # AddressSanitizer
```

## Development

```bash
bazel clean && bazel build //...                          # clean rebuild
bazel query 'deps(//examples:container_benchmark)'        # view dependency graph
```

## Resources

- [Bazel Documentation](https://bazel.build/docs)
- [Bazel C++ Guide](https://bazel.build/docs/cc-toolchain-config-reference)
- [Bazel Query Language](https://bazel.build/query/quickstart)

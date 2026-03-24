# AnyArr

**AnyArr** is a single-header C library providing a dynamic array, a hash map, and a tagged-union `Any` value type, backed by a virtual-memory arena allocator. It is designed to be as fast as possible while remaining easy and flexible to use.

```c
#define ANYARR_IMPLEMENTATION
#include "anyarr.h"
```

## Before You Start

The library only works on C11 and later with only GCC and Clang due to specific compiler optimizations. The library is still under active development so things may change regularly.

By default the namespace of heterogeneous values is `Any` which can likely cause namespace issues so there's a macro to fix namespace conflicts.

```c
#define ANYARR_IMPLEMENTATION
#define ANY_NAMESPACE AnyArr
#include "anyarr.h"
```

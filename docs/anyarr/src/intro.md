# AnyArr

**AnyArr** is a single-header GCC/Clang 11 library providing a dynamic array, a hash map, and a tagged-union `Any` value type, backed by a virtual-memory arena allocator. It is designed to be as fast as possible while remaining easy and flexible to use.

```c
#define ANYARR_IMPLEMENTATION
#include "anyarr.h"
```

## Before You Start

The library only works on C11 and later with only GCC and Clang due to specific compiler optimizations. It also provides `nullptr` which falls back to `(void*) 0` as macro if not in C23.
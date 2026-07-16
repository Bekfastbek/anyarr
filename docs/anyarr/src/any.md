## Overview

It is a 16 byte typed union struct. It holds a type (uint8_t) and the underlying datatype.

So each element takes 16 bytes even if it's just a simple bool or a char.

## Structure Visualization

```
Byte:  0        1                   7                  15
       +--------+--------------------------------------+
       | type   | Padding (7 bytes) |  data (8 bytes)  |   ← normal variant
       +--------+--------------------------------------+
                1                                      15
       +--------+--------------------------------------+
       | type   |       str[0] ... str[14] \0          |   ← SSO variant (string ≤ 14 chars)
       +--------+--------------------------------------+
                1      2                               15
       +--------+------+-------------------------------+
       | type   | len  |          b[0]..b[13]          |   ← SBO variant (blob ≤ 14 bytes)
       +--------+------+-------------------------------+
```

## DataTypes

- Null
- Bool
- Char
- Int (signed char, short, int, long, long long -> int64_t)
- UInt (unsigned char, st, long, long long -> uint64_t)
- Float
- Double
- Strings (Arena allocated String Buffer)
- Small Strings (SSO 14 elements + 1 null terminator)
- Blob (Arena allocated Byte Buffer)
- Small Blobs (SBO 14 elements + 1 length byte)
- Ptr (User Managed Generic pointers)
- Arrays (Nested)
- HashMaps (Nested)

Note: Small String/Blob Optimization is structured to fit within a single L1 cache line fetch, avoiding a second memory access for small values which has huge performance gains

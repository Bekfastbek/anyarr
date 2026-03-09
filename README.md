# ANYARR

Lightweight C11 Library for creating dynamic arrays which can store multiple datatypes.

### Supported DataTypes:
- Bool
- Char
- Int
- Float
- Double
- String

You might ask what's special about this library? Well the library behaves like python but in C so it should lead to a better DX. It also uses tagged union with _Generic macro to cast the datatype which is much safer and less of a headache to deal with rather than generic/void pointers.

### Features:
- It can hold multiple datatypes in the array
- Incredible performance for small strings because of the SSO (Small String Optimization) so any strings which are <= 15 bytes will not have to be malloc'd which prevents a second malloc call
- Automatic type inference with _Generic macros
- Great type checking features and fallback getters to help with safe access
- It's all in a single header so it's just a drop-in library
- If you are having namespace conflicts with "Any" then you can define ANY_NAMESPACE to whatever variable name you want in a macro before importing the library

```C
#define ANY_NAMESPACE AnyArr
#include "include/anyarr.h"
```
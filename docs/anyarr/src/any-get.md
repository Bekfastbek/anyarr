## Reading Values

**get_any():** Returns the datatype of the element.

```c
Any val = assign_any("String");
const char* *out;
get_any(&val, &out); // *out will have the returning datatype since you cannot dereference val itself, for strings it needs to be const char* and not char* since the macro defaults to any_get_char instead of any_get_string otherwise
```

**any_print():** Prints out the underlying datatype or metadata structure of an array or variable

```c
HashMap Map;
map_init(&Map);
assign_map(&Map);
map_put(&Map, "Active", true);
DynamicArray Array;
array_init(&Array);
const Any val = assign_array(&Array);
array_append(&Array, "bek");
array_append(&Array, &Map);
any_print(&val, 0);
```

The output will look like this:

```c
[
  string(sso): "bek"
  {
    Active:
      bool: true
  }
]
```

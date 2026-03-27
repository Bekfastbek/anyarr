## Reading Values

**get_any():** Returns the datatype of the element.

```c
Any val = assign_any("String");
const char* *out;
get_any(&val, &out); // *out will have the returning datatype since you cannot dereference val itself, for strings it needs to be const char* and not char* since the macro defaults to any_get_char instead of any_get_string otherwise
```

**path_get():** Returns the datatype based on path traversal.

```c
HashMap(Map);
map_put(&Map, "Active", true);
DynamicArray(Array);
array_append(&Array, "bek");
array_append(&Array, &Map);
bool out;
path_get(&Array, "[1].Active", &out);
printf("%d", out);
```

**any_print():** Prints out the underlying datatype or metadata structure of an array or variable

```c
HashMap(Map);
map_put(&Map, "Active", true);
DynamicArray(Array);
array_append(&Array, "bek");
array_append(&Array, &Map);
any_print(&Array);
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

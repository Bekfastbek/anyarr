## Assigning Values

**assign_any():** Automatically infers the underlying datatype and assigns a value.

**Return Type:** Any

```c
assign_any('A'); // Automatically assigns to char
assign_any(true); // Automatically assigns to bool
assign_any(42); // Automatically assigns to int
assign_any(42U); // Automatically assigns to unsigned int
assign_any(3.14f); // Automatically assigns to float
assign_any(0.00223); // Automatically assigns to double
assign_any("This is a string."); // Automatically assigns to string
```

for blobs:

```c
uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    Blob blob;
    blob.ptr = data;
    blob.size = sizeof(data);
    Any val = assign_any(&blob);
```

for void*:

```c
struct Player {
    const char* name;
    bool alive;
    uint8_t health;
} player = {"Bek", true, 80};
assign_any(&player); 
/* It only stores the address of the pointer 
 * ownership and everything else is handled by caller
 * it's not possible to dereference the pointer in my library
*/
```

**DynamicArray:** Wraps a DynamicArray into an Any value. The array can still be used directly through its pointer for appending and indexing.

```c
DynamicArray Array;
array_init(&Array);
Any wrapper = assign_array(&Array);
array_append(&Array, assign_any(20)); // Stores an integer
array_append(&Array, assign_any("String")); // Stores a small string
```

**Hashmaps:** Wraps a HashMap into an Any value. The map can still be used directly through its pointer for insertion and lookup.

```c
HashMap Map;
map_init(&Map);
Any wrapper = assign_map(&Map);
map_put(&Map, "Key", assign_any("Value"));
map_put(&Map, "Key1", assign_any(20));
```

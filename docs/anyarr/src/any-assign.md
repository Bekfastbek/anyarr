## Assigning Values

**assign_any():** Automatically infers the underlying datatype and assigns a value to an array.

**Retrun Type:** Any

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

for arrays:

```c

```

for hashmaps:

```c

```

for void*:

```c

```

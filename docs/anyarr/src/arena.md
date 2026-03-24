## Important Notes

The arena owns all the memory that you allocate. If you want to reclaim the memory then you must reset the arena itself or make it go out of scope (if the arena is scoped) since you can't do free per value.

Only exception is the pointer datatype where the caller is responsible for the management.

Small String/Blob Values are self-contained and their lifetime is tied to the lifetime of Any struct.

As of now the arena is single threaded but in future there is a plan to spawn a new arena in a new thread through a system of thread pools.

---

## Arena Functions

**arena_reset():** Wipes the entire arena by setting used to 0. Use this to throw all allocations and start fresh.

**Return Type:** anyarr_result

```c
arena_reset(anyarr_arena); // Only accepts the pointer to the arena
```


**arena_save():** Returns the current position of the arena as a checkpoint. Use this to mark a point you want to return to later. Accepts `NULL` and defaults to the global arena.

**Return Type:** size_t

```c
size_t cp = arena_save(anyarr_arena);
size_t cp = arena_save(NULL); // Defaults to global arena
```

**arena_restore():** Rewinds the arena back to a previously saved checkpoint and zeroes the reclaimed region. Everything allocated after the save point is invalidated. Accepts `NULL` and defaults to the global arena. If the checkpoint is already past the current position, it does nothing.

**Return Type:** void

```c
arena_restore(anyarr_arena, cp);
arena_restore(NULL, cp);
```

**ARENA_TEMP:** Macro which just does the same thing as saving and restoring the arena and only works with the global arena. So instead of

```c
int main() {
    ...
    ...
    ...
    ...
    size_t cp = arena_save(NULL);
    ...
    ...
    ...       // Your code
    ...
    ...
    ...
    arena_restore(NULL, cp); // Restore here
}
```

You can do

```c
int main() {
    ...
    ...
    ...
    ...
    ARENA_TEMP cp = arena_save(NULL);
    ...
    ...
    ...  // No need to manually restore checkpoint, arena cleans up when it's out of scope
}
```

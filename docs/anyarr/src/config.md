## Configuration

The library provides an extensive set of configurations through macros. Here is the list of all macros:

---



**ANYARR_IMPLEMENTATION:** Global Arena Declaration

Must be defined in exactly one translation unit before including the header.

---

**ANYARR_RESERVE_SIZE:** 8tb (Windows) 128tb (POSIX)

It can be used to set the virtual memory allocation size though you probably don't need to change it unless you are running a 32 bit processor where the virtual memory space is limited to 4gb.

---

**ANYARR_COMMIT_CHUNK:** 16mb

The memory gets committed in 16mb chunks by default. Reduce the size to reduce the memory overhead or increase the size to reduce overall syscalls for allocation-heavy workloads.

---

**ARENA_NAMESPACE:** `Arena`

Change if you are having namespace conflict with the default type name.

---

**ANY_NAMESPACE:** `Any`

Change if you are having namespace conflict with the default type name.

---

/*
 * This library should work with GCC/Clang C11 onwards flawlessly but C99 needs a lot of setup to work and generally not recommended
 * To make it work on C99, you might need to use the GNU C99 standard instead of strict C99 for anonymous structs to work
 * There is also no support for generic macros so remove the macros at the bottom of the file and stick with specific datatypes
 * Another note, cleanup attribute is only a part of GCC/Clang not a part of C standard so it won't work with other compilers
 * Also this library is made to be as fast as possible while being easy to use so it's not exactly efficient on memory usage
 * As of now this library does not guarantee thread safety and documentation is in the README of the repo
 * It only works on MinGW on Windows and not at all on MSVC
 * A good idea would be making a thread pool with each thread holding an individual arena if you want to implement your own thread pool
*/

#ifndef ANYARR_H
#define ANYARR_H
#if __STDC_VERSION__ >= 202311L
    // Ignore stdbool import since C23 has bool types built-in
#else
#   include <stdbool.h>
#endif
#if defined(__AVX512BW__) || defined(__AVX2__) || defined(__AVX__)
#   include <immintrin.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000L
#  ifndef nullptr
#    define nullptr ((void*)0)
#  endif
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#if defined(_WIN32) || defined(_WIN64)
#   define ANYARR_PLATFORM_WINDOWS
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#else
#   include <sys/mman.h>
#   include <sys/random.h>
#   ifndef MAP_ANONYMOUS
#       define MAP_ANONYMOUS MAP_ANON
#   endif
#endif

#ifndef ARENA_CTX
#   define ARENA_CTX anyarr_arena
#endif

#ifndef ANYARR_RESERVE_SIZE
#   define ANYARR_RESERVE_SIZE (1ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)
#endif

#ifndef ARENA_COMMIT_CHUNK
#   define ARENA_COMMIT_CHUNK (16ULL * 1024ULL * 1024ULL)
#endif

#ifndef ARENA_NAMESPACE
#   define ARENA_NAMESPACE Arena
#endif

#ifndef ANY_NAMESPACE
#   define ANY_NAMESPACE Any
#endif

#ifndef ANYARR_WALKER_DEPTH
#   define ANYARR_WALKER_DEPTH 16
#endif

#ifndef ANYARR_PREFETCH_DISTANCE
#   define ANYARR_PREFETCH_DISTANCE 4
#endif

#ifndef ANYARR_L2_SIZE
#   define ANYARR_L2_SIZE 256
#endif

#define WALK_SHALLOW 1
#define WALK_DEEP 0 // 0 means maximum recursion


typedef enum {
    ANYARR_OK = 0x00,
    ANYARR_EQUAL = 0x01,
    ANYARR_NOT_EQUAL =0x02,
    ANYARR_ERR_OOM = 0x03,
    ANYARR_ERR_NULLPTR = 0x04,
    ANYARR_ERR_OUT_OF_BOUNDS = 0x05,
    ANYARR_ERR_EMPTY = 0x06,
    ANYARR_ERR_EMPTY_KEY = 0x07,
    ANYARR_ERR_TYPE_MISMATCH = 0x08,
} anyarr_result;

enum Type {
    TYPE_NULL,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_INT,
    TYPE_UINT,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_STRING_SMALL,
    TYPE_BLOB,
    TYPE_BLOB_SMALL,
    TYPE_PTR,   // Not supporting a footgun so the ownership is on your hands not arena and only providing clone and comparison safely with any_print only providing pointer address, but I was thinking of supporting void* to be stored in Arena in the future
    TYPE_ARRAY,
    TYPE_MAP
};

enum Num_Type {
    TYPE_I64,
    TYPE_U64,
    TYPE_I32,
    TYPE_U32,
    TYPE_I16,
    TYPE_U16,
    TYPE_I8,
    TYPE_U8,
    TYPE_F32,
    TYPE_F64
};


static inline anyarr_result handle_error(const uint8_t error_code, const uint32_t line, const char* file) {
    switch (error_code) {
        case ANYARR_ERR_OOM:
            fprintf(stderr, "[ANYARR] Out of Memory. Exiting...\nLine: %d\nFile: %s\n", line, file);
            abort(); // abort since unless something catastrophic happened, you aren't supposed to run out of virtual memory
        case ANYARR_ERR_NULLPTR:
            fprintf(stderr, "[ANYARR] Null pointer was passed in an unrecoverable state. Exiting...\nLine: %d\nFile: %s\n", line, file);
            abort();
        case ANYARR_ERR_OUT_OF_BOUNDS:
            fprintf(stderr, "[ANYARR] Index out of bounds.\nLine: %d\nFile: %s\n", line, file);
            break;
        case ANYARR_ERR_EMPTY:
            fprintf(stderr, "[ANYARR] Index not found.\nLine: %d\nFile: %s\n", line, file);
            break;
        case ANYARR_ERR_EMPTY_KEY:
            fprintf(stderr, "[ANYARR] Key not found.\nLine: %d\nFile: %s\n", line, file);
            break;
        case ANYARR_ERR_TYPE_MISMATCH:
            fprintf(stderr, "[ANYARR] Type mismatch.\nLine: %d\nFile: %s\n", line, file);
            break;
        default:
            fprintf(stderr, "[ANYARR] Error: %10x\nLine: %d\nFile: %s\n", error_code, line, file);
            break;
    }
    return error_code;
}


typedef struct {
    uint8_t *base;
    size_t used;
    size_t committed;
    size_t reserved;
} ARENA_NAMESPACE;

#if defined(ANYARR_IMPLEMENTATION)
ARENA_NAMESPACE anyarr_arena_instance;
ARENA_NAMESPACE *ARENA_CTX = nullptr;
#else
extern ARENA_NAMESPACE anyarr_arena_instance;
extern ARENA_NAMESPACE *ARENA_CTX;
#endif


static inline uint64_t make_seed(void) {
    uint64_t seed = 0;
#if defined(ANYARR_PLATFORM_WINDOWS) // Spent WAY TOO LONG figuring out how to get random number without BCrypt but here is reference: https://github.com/jedisct1/libsodium/blob/master/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c
#define RtlGenRandom SystemFunction036
    BOOLEAN NTAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
    RtlGenRandom(&seed, sizeof(seed));
#else
    const uint8_t error = getentropy(&seed, sizeof(seed)); // getrandom() is not required for this use case, it only needs to init once and the seed !>255
    if (error != 0) {
        handle_error(error, __LINE__, __FILE__);
    }
#endif
    if (seed == 0) {
        fprintf(stderr, "Seed failed. Aborting...");
        abort();
    }
    return seed;
}


static inline size_t arena_align_up(const size_t n) {
    return (n + 15) & ~15;
}


static inline anyarr_result arena_commit(ARENA_NAMESPACE *a, const size_t extra) {
    size_t new_committed = (a->committed + extra + ARENA_COMMIT_CHUNK - 1) & ~(ARENA_COMMIT_CHUNK - 1);
    if (new_committed > a->reserved) {
        new_committed = a->reserved;
    }
    if (new_committed <= a->committed) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
    size_t delta = new_committed - a->committed;
#if defined(ANYARR_PLATFORM_WINDOWS)
    if (VirtualAlloc(a->base + a->committed, delta, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
#else
    if (mprotect(a->base + a->committed, delta, PROT_READ | PROT_WRITE) != 0) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
#endif
    a->committed = new_committed;
    return ANYARR_OK;
}


static inline void auto_init(void);

static inline anyarr_result arena_alloc(ARENA_NAMESPACE *arena, const size_t size, void **out) {
    if (__builtin_expect(arena == nullptr, 0)) {
        auto_init();
        arena = ARENA_CTX;
    }
    if (arena->base == nullptr || size == 0 || out == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    const size_t new_used = arena->used + arena_align_up(size);
    if (new_used > arena->reserved) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
    if (new_used > arena->committed) {
        if (arena_commit(arena, new_used - arena->committed) != ANYARR_OK) {
            return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
        }
    }
    *out = arena->base + arena->used;
    arena->used = new_used;
    return ANYARR_OK;
}


static inline anyarr_result arena_init(ARENA_NAMESPACE *arena) {
    if (arena == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    size_t reserve_size = (ANYARR_RESERVE_SIZE + ARENA_COMMIT_CHUNK - 1) & ~(ARENA_COMMIT_CHUNK - 1);
#if defined(ANYARR_PLATFORM_WINDOWS)
    void *base = VirtualAlloc(nullptr, reserve_size, MEM_RESERVE, PAGE_NOACCESS);
    if (base == nullptr) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
#else
    void *base = mmap(nullptr, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
    }
#   ifdef MADV_HUGEPAGE
    madvise(base, reserve_size, MADV_HUGEPAGE);
#   endif
#endif
    arena->base = (uint8_t *) base;
    arena->used = 0;
    arena->committed = 0;
    arena->reserved = reserve_size;
    return ANYARR_OK;
}


static inline anyarr_result arena_reset(ARENA_NAMESPACE *arena) {
    if (arena == nullptr || arena->base == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    arena->used = 0;
    return ANYARR_OK;
}


static inline anyarr_result arena_free(ARENA_NAMESPACE *arena) {
    if (arena->base == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
#if defined(ANYARR_PLATFORM_WINDOWS)
    VirtualFree(arena->base, 0, MEM_RELEASE);
#else
    munmap(arena->base, arena->reserved);
#endif
    arena->base = nullptr;
    arena->used = 0;
    arena->committed = 0;
    arena->reserved = 0;
    return ANYARR_OK;
}


static inline size_t arena_save(const ARENA_NAMESPACE *arena) {
    const ARENA_NAMESPACE *Arena = arena;
    if (Arena == nullptr) {
        Arena = ARENA_CTX;
    }
    return Arena->used;
}


static inline void arena_restore(ARENA_NAMESPACE *arena, const size_t saved) {
    if (arena == nullptr) {
        arena = ARENA_CTX;
    }
    if (saved >= arena->used) {
        return;
    }
    arena->used = saved;
}


static inline void checkpoint_cleanup(const size_t *cp) {
    arena_restore(nullptr, *cp);
}

#define ARENA_TEMP __attribute__((cleanup(checkpoint_cleanup))) size_t


static inline void auto_cleanup(void) {
    if (ARENA_CTX != nullptr) {
        arena_free(ARENA_CTX);
        ARENA_CTX = nullptr;
    }
}

static inline void auto_init() {
    if (ARENA_CTX != nullptr) {
        return;
    }
    arena_init(&anyarr_arena_instance);
    ARENA_CTX = &anyarr_arena_instance;
    atexit(auto_cleanup);
}


static inline void arena_cleanup(ARENA_NAMESPACE *ap) {
    if (ap) {
        arena_free(ap);
    }
}

#define ARENA_SCOPED __attribute__((cleanup(arena_cleanup))) ARENA_NAMESPACE


typedef struct DynamicArray DynamicArray;
typedef struct HashMap HashMap;
typedef struct Blob Blob;

typedef struct {
    union {
        struct {
            uint8_t type;

            union {
                bool b;
                char c;
                int64_t i;
                uint64_t u;
                float f;
                double d;
                char *s;
                Blob *l;
                void *p;
                DynamicArray *a;
                HashMap *m;
            } data;
        };

        struct {
            uint8_t _type_sso;
            char small_buf[15];
            // From my testing 15 elements seemed to be much faster than 16 bytes around ~30% on append speeds, going above introduces the next set of bytes in l1 cache which means more padding and more work for cpu
        };

        struct {
            uint8_t _type_sbo;
            uint8_t len;
            uint8_t small_blob[14];
            // For small blobs like a short b64 stream, 14 elements on this because 1 byte is taken by len
        };
    };
} ANY_NAMESPACE;

struct DynamicArray {
    ANY_NAMESPACE *data;
    size_t size;
    size_t capacity;
};

#define CTRL_EMPTY 0xFF
#define CTRL_DELETED 0xFE
typedef struct {
    ANY_NAMESPACE key;
    ANY_NAMESPACE value;
} Slots;
struct HashMap {
    // By default, the fingerprint stores 0xFF, but it gets override by an actual fingerprint and when it gets deleted it again gets override to 0xFE making it act also as control byte
    uint8_t *fingerprint;
    Slots *slots;
    size_t size;
    size_t capacity;
    size_t tombstone;   // Keeps track of how many slots are deleted so when we trigger a resize we already know how many entries are deleted instead of empty
    uint64_t hash_seed;
    uint64_t hash_seed_c1;
};

struct Blob {
    uint8_t *ptr;
    size_t size;
};

typedef struct {
    uint8_t type;
    size_t index;
    size_t bound;
    uint8_t *fingerprint;
    ANY_NAMESPACE *data;
    Slots *slots;
    const ANY_NAMESPACE *last_key;
} AnyIter;


#define ANYARR_ARG1(_1, N, ...) N
#define ANYARR_ARG2(_1, _2, N, ...) N
#define ANYARR_ARG3(_1, _2, _3, N, ...) N
#define ANYARR_ARG4(_1, _2, _3, _4, N, ...) N
#define ANYARR_ARG5(_1, _2, _3, _4, _5, N, ...) N


static inline void arena_alloc_aligned64(ARENA_NAMESPACE *arena, const size_t size, void **out) {
    uint8_t *raw = nullptr;
    arena_alloc(arena, size + 63, (void **) &raw);
    *out = (void *) (((uintptr_t) raw + 63) & ~(uintptr_t) 63);
}


static inline void _map_init_arena(HashMap *m, ARENA_NAMESPACE *arena) {
    if (m == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    static const uint64_t WY1 = 0xe7037ed1a0b428dbull;
    m->size = 0;
    m->capacity = 64;
    m->tombstone = 0;
    arena_alloc_aligned64(arena, m->capacity * sizeof(uint8_t) + 64, (void **) &m->fingerprint);
    arena_alloc_aligned64(arena, m->capacity * sizeof(Slots), (void **) &m->slots);
    memset(m->fingerprint, CTRL_EMPTY, m->capacity + 64);
    m->hash_seed = make_seed();
    m->hash_seed_c1 = m->hash_seed ^ WY1;
}


static inline void _map_init_impl(HashMap *m) {
    _map_init_arena(m, ARENA_CTX);
}

#define map_init(...) ANYARR_ARG2(__VA_ARGS__, _map_init_arena, _map_init_impl) (__VA_ARGS__)


static inline void _array_init_arena(DynamicArray *buf, ARENA_NAMESPACE *arena) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    buf->size = 0;
    buf->capacity = 4;
    arena_alloc(arena, buf->capacity * sizeof(ANY_NAMESPACE), (void **) &buf->data);
}


static inline void _array_init_impl(DynamicArray *buf) {
    _array_init_arena(buf, ARENA_CTX);
}

#define array_init(...) ANYARR_ARG2(__VA_ARGS__, _array_init_arena, _array_init_impl) (__VA_ARGS__)


static inline ANY_NAMESPACE assign_null(void) {
    return (ANY_NAMESPACE){TYPE_NULL};
}


static inline ANY_NAMESPACE assign_bool(const _Bool b) {
    return (ANY_NAMESPACE){TYPE_BOOL, .data.b = b};
}


static inline ANY_NAMESPACE assign_char(const char c) {
    return (ANY_NAMESPACE){TYPE_CHAR, .data.c = c};
}


static inline ANY_NAMESPACE assign_int(const int64_t i) {
    return (ANY_NAMESPACE){TYPE_INT, .data.i = i};
}


static inline ANY_NAMESPACE assign_uint(const uint64_t u) {
    return (ANY_NAMESPACE){TYPE_UINT, .data.u = u};
}


static inline ANY_NAMESPACE assign_float(const float f) {
    return (ANY_NAMESPACE){TYPE_FLOAT, .data.f = f};
}


static inline ANY_NAMESPACE assign_double(const double d) {
    return (ANY_NAMESPACE){TYPE_DOUBLE, .data.d = d};
}


static inline ANY_NAMESPACE _assign_string_arena(const char *s, ARENA_NAMESPACE *arena) {
    if (s == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    const size_t len = strlen(s);
    if (len < 15) {
        ANY_NAMESPACE val = {._type_sso = TYPE_STRING_SMALL};
        memcpy(val.small_buf, s, len + 1);
        return val;
    }
    char *dup;
    arena_alloc(arena, len + 1, (void **) &dup);
    memcpy(dup, s, len + 1);
    return (ANY_NAMESPACE){.type = TYPE_STRING, .data.s = dup};
}


static inline ANY_NAMESPACE _assign_string_impl(const char *s) {
    return _assign_string_arena(s, ARENA_CTX);
}

#define assign_string(...) ANYARR_ARG2(__VA_ARGS__, _assign_string_arena, _assign_string_impl) (__VA_ARGS__)


static inline ANY_NAMESPACE _assign_blob_arena(const Blob *l, ARENA_NAMESPACE *arena) {
    if (l == nullptr || l->ptr == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (l->size < 15) {
        ANY_NAMESPACE val = {._type_sbo = TYPE_BLOB_SMALL, .len = l->size};
        memcpy(val.small_blob, l->ptr, l->size);
        return val;
    }
    Blob *dup;
    arena_alloc(arena, sizeof(Blob), (void **) &dup);
    arena_alloc(arena, l->size, (void **) &dup->ptr);
    memcpy(dup->ptr, l->ptr, l->size);
    dup->size = l->size;
    return (ANY_NAMESPACE){.type = TYPE_BLOB, .data.l = dup};
}


static inline ANY_NAMESPACE _assign_blob_impl(const Blob *l) {
    return _assign_blob_arena(l, ARENA_CTX);
}

#define assign_blob(...) ANYARR_ARG2(__VA_ARGS__, _assign_blob_arena, _assign_blob_impl) (__VA_ARGS__)


static inline ANY_NAMESPACE _assign_array_arena(DynamicArray *a, ARENA_NAMESPACE *arena) {
    if (a == nullptr) {
        DynamicArray *heap_arr;
        arena_alloc(arena, sizeof(DynamicArray), (void **) &heap_arr);
        array_init(heap_arr, arena);
        return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = heap_arr};
    }
    return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = a};
}


static inline ANY_NAMESPACE _assign_array_impl(DynamicArray *a) {
    return _assign_array_arena(a, ARENA_CTX);
}

#define assign_array(...) ANYARR_ARG2(__VA_ARGS__, _assign_array_arena, _assign_array_impl) (__VA_ARGS__)


static inline ANY_NAMESPACE _assign_map_arena(HashMap *m, ARENA_NAMESPACE *arena) {
    if (m == nullptr) {
        HashMap *heap_map;
        arena_alloc(arena, sizeof(HashMap), (void **) &heap_map);
        map_init(heap_map, arena);
        return (ANY_NAMESPACE){TYPE_MAP, .data.m = heap_map};
    }
    return (ANY_NAMESPACE){TYPE_MAP, .data.m = m};
}


static inline ANY_NAMESPACE _assign_map_impl(HashMap *m) {
    return _assign_map_arena(m, ARENA_CTX);
}

#define assign_map(...) ANYARR_ARG2(__VA_ARGS__, _assign_map_arena, _assign_map_impl) (__VA_ARGS__)


static inline ANY_NAMESPACE assign_ptr(void *p) {
    if (p == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    return (ANY_NAMESPACE){TYPE_PTR, .data.p = p};
}


static inline ANY_NAMESPACE any_disp_bool(const _Bool x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_bool(x);
}

static inline ANY_NAMESPACE any_disp_char(const char x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_char(x);
}

static inline ANY_NAMESPACE any_disp_i64(const int64_t x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_int(x);
}

static inline ANY_NAMESPACE any_disp_u64(const uint64_t x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_uint(x);
}

static inline ANY_NAMESPACE any_disp_f32(const float x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_float(x);
}

static inline ANY_NAMESPACE any_disp_f64(const double x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_double(x);
}

static inline ANY_NAMESPACE any_disp_str(const char *x, ARENA_NAMESPACE *arena) {
    return _assign_string_arena(x, arena);
}

static inline ANY_NAMESPACE any_disp_blob(const Blob *x, ARENA_NAMESPACE *arena) {
    return _assign_blob_arena(x, arena);
}

static inline ANY_NAMESPACE any_disp_arr(DynamicArray *x, ARENA_NAMESPACE *arena) {
    return _assign_array_arena(x, arena);
}

static inline ANY_NAMESPACE any_disp_map(HashMap *x, ARENA_NAMESPACE *arena) {
    return _assign_map_arena(x, arena);
}

static inline ANY_NAMESPACE any_disp_ptr(void *x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return assign_ptr(x);
}

// Passthrough so an already built Any (e.g. a nested map or array) can be handed straight to assign_any/append/put without being wrapped again
static inline ANY_NAMESPACE any_disp_any(const ANY_NAMESPACE x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    return x;
}


// Since we covered every single datatype, natural fallback to void* would allow us to store void* conveniently
#define _assign_any_dispatch(x, arena) _Generic((x),    \
ANY_NAMESPACE: any_disp_any,                            \
_Bool: any_disp_bool,                                   \
char: any_disp_char,                                    \
signed char: any_disp_i64,                              \
short: any_disp_i64,                                    \
int: any_disp_i64,                                      \
long: any_disp_i64,                                     \
long long: any_disp_i64,                                \
unsigned char: any_disp_u64,                            \
unsigned short: any_disp_u64,                           \
unsigned int: any_disp_u64,                             \
unsigned long: any_disp_u64,                            \
unsigned long long: any_disp_u64,                       \
float: any_disp_f32,                                    \
double: any_disp_f64,                                   \
char*: any_disp_str,                                    \
const char*: any_disp_str,                              \
Blob*: any_disp_blob,                                   \
DynamicArray*: any_disp_arr,                            \
HashMap*: any_disp_map,                                 \
default: any_disp_ptr                                   \
)((x), (arena))

#define _assign_any_impl(x) _assign_any_dispatch((x), ARENA_CTX)
#define _assign_any_arena(x, arena) _assign_any_dispatch((x), (arena))
#define assign_any(...) ANYARR_ARG2(__VA_ARGS__, _assign_any_arena, _assign_any_impl) (__VA_ARGS__)

// Non-owning string key for lookups only. It just references the caller's buffer, no strlen and no copy here, so the single strlen happens once inside the hash instead of twice. Safe because the key only lives for the duration of the get/remove call and is never stored
static inline ANY_NAMESPACE any_key_str(const char *x, const ARENA_NAMESPACE *arena) {
    (void) arena;
    if (x == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    return (ANY_NAMESPACE){.type = TYPE_STRING, .data.s = (char *) x};
}


#define _assign_key_dispatch(x, arena) _Generic((x),    \
_Bool: any_disp_bool,                                   \
char: any_disp_char,                                    \
signed char: any_disp_i64,                              \
short: any_disp_i64,                                    \
int: any_disp_i64,                                      \
long: any_disp_i64,                                     \
long long: any_disp_i64,                                \
unsigned char: any_disp_u64,                            \
unsigned short: any_disp_u64,                           \
unsigned int: any_disp_u64,                             \
unsigned long: any_disp_u64,                            \
unsigned long long: any_disp_u64,                       \
float: any_disp_f32,                                    \
double: any_disp_f64,                                   \
char*: any_disp_str,                                    \
const char*: any_disp_str                               \
)((x), (arena))

#define _assign_key_impl(x) _assign_key_dispatch((x), ARENA_CTX)
#define _assign_key_arena(x, arena) _assign_key_dispatch((x), (arena))
#define assign_key(...) ANYARR_ARG2(__VA_ARGS__, _assign_key_arena, _assign_key_impl) (__VA_ARGS__)


#define _lookup_key_dispatch(x, arena) _Generic((x),    \
_Bool: any_disp_bool,                                   \
char: any_disp_char,                                    \
signed char: any_disp_i64,                              \
short: any_disp_i64,                                    \
int: any_disp_i64,                                      \
long: any_disp_i64,                                     \
long long: any_disp_i64,                                \
unsigned char: any_disp_u64,                            \
unsigned short: any_disp_u64,                           \
unsigned int: any_disp_u64,                             \
unsigned long: any_disp_u64,                            \
unsigned long long: any_disp_u64,                       \
float: any_disp_f32,                                    \
double: any_disp_f64,                                   \
char*: any_key_str,                                     \
const char*: any_key_str                                \
)((x), (arena))

#define _lookup_key(x) _lookup_key_dispatch((x), ARENA_CTX)


static inline anyarr_result any_get_bool(const ANY_NAMESPACE *val, _Bool *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_BOOL) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.b;
    return ANYARR_OK;
}


static inline anyarr_result any_get_char(const ANY_NAMESPACE *val, char *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_CHAR) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.c;
    return ANYARR_OK;
}


static inline anyarr_result any_get_int(const ANY_NAMESPACE *val, int64_t *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_INT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.i;
    return ANYARR_OK;
}


static inline anyarr_result any_get_uint(const ANY_NAMESPACE *val, uint64_t *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_UINT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.u;
    return ANYARR_OK;
}


static inline anyarr_result any_get_float(const ANY_NAMESPACE *val, float *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_FLOAT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.f;
    return ANYARR_OK;
}


static inline anyarr_result any_get_double(const ANY_NAMESPACE *val, double *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_DOUBLE) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.d;
    return ANYARR_OK;
}


static inline anyarr_result any_get_string(const ANY_NAMESPACE *val, const char **out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (val->type == TYPE_STRING) {
        *out_value = val->data.s;
        return ANYARR_OK;
    } else if (val->type == TYPE_STRING_SMALL) {
        *out_value = val->small_buf;
        return ANYARR_OK;
    }
    return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
}


static inline anyarr_result any_get_blob(const ANY_NAMESPACE *val, Blob *out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (val->type == TYPE_BLOB) {
        out_value->ptr = val->data.l->ptr;
        out_value->size = val->data.l->size;
        return ANYARR_OK;
    } else if (val->_type_sbo == TYPE_BLOB_SMALL) {
        out_value->ptr = (uint8_t *) val->small_blob;
        out_value->size = val->len;
        return ANYARR_OK;
    }
    return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
}


static inline anyarr_result any_get_ptr(const ANY_NAMESPACE *val, void **out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_PTR) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.p;
    return ANYARR_OK;
}


static inline anyarr_result any_get_array(const ANY_NAMESPACE *val, DynamicArray **out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_ARRAY) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.a;
    return ANYARR_OK;
}


static inline anyarr_result any_get_map(const ANY_NAMESPACE *val, HashMap **out_value) {
    if (val == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    } else if (val->type != TYPE_MAP) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
    *out_value = val->data.m;
    return ANYARR_OK;
}


static inline AnyIter any_iter(const ANY_NAMESPACE *root);
static inline ANY_NAMESPACE *any_iter_next(AnyIter *it);

static inline void _any_key_print(const ANY_NAMESPACE *key) {
    switch (key->type) {
        case TYPE_STRING:
            printf("%s", key->data.s);
            break;
        case TYPE_STRING_SMALL:
            printf("%s", key->small_buf);
            break;
        case TYPE_BOOL:
            printf("%s", key->data.b ? "true" : "false");
            break;
        case TYPE_CHAR:
            printf("%c", key->data.c);
            break;
        case TYPE_INT:
#if defined(ANYARR_PLATFORM_WINDOWS)
            printf("%lld", key->data.i);
#else
            printf("%ld", key->data.i);
#endif
            break;
        case TYPE_UINT:
#if defined(ANYARR_PLATFORM_WINDOWS)
            printf("%llu", key->data.u);
#else
            printf("%lu", key->data.u);
#endif
            break;
        case TYPE_FLOAT:
            printf("%f", key->data.f);
            break;
        case TYPE_DOUBLE:
            printf("%lf", key->data.d);
            break;
        default:
            break;
    }
}

static inline anyarr_result _any_print_impl(ANY_NAMESPACE *val, const int depth) {
#define INDENT() for (int _i = 0; _i < depth; _i++) printf("  ")

    switch (val->type) {
        case TYPE_NULL:
            INDENT();
            printf("null\n");
            return ANYARR_OK;

        case TYPE_BOOL:
            INDENT();
            if (val->data.b) {
                printf("bool: true\n");
            } else {
                printf("bool: false\n");
            }
            return ANYARR_OK;

        case TYPE_CHAR:
            INDENT();
            printf("char: %c\n", val->data.c);
            return ANYARR_OK;

        case TYPE_INT:
            INDENT();
#if defined(ANYARR_PLATFORM_WINDOWS)
            printf("int64_t: %lld\n", val->data.i);
#else
            printf("int_64_t: %ld\n", val->data.i);
#endif
            return ANYARR_OK;

        case TYPE_UINT:
            INDENT();
#if defined(ANYARR_PLATFORM_WINDOWS)
            printf("uint64_t: %llu\n", val->data.u);
#else
            printf("uint64_t: %lu\n", val->data.u);
#endif
            return ANYARR_OK;

        case TYPE_FLOAT:
            INDENT();
            printf("float: %f\n", val->data.f);
            return ANYARR_OK;

        case TYPE_DOUBLE:
            INDENT();
            printf("double: %lf\n", val->data.d);
            return ANYARR_OK;

        case TYPE_STRING:
            INDENT();
            printf("string(heap): \"%s\"\n", val->data.s);
            return ANYARR_OK;

        case TYPE_STRING_SMALL:
            INDENT();
            printf("string(sso): \"%s\"\n", val->small_buf);
            return ANYARR_OK;

        case TYPE_BLOB: {
            INDENT();
            printf("blob(heap, %zu bytes): [ ", val->data.l->size);
            for (size_t i = 0; i < val->data.l->size; i++) {
                printf("%02x ", val->data.l->ptr[i]);
            }
            printf("]\n");
            return ANYARR_OK;
        }

        case TYPE_BLOB_SMALL: {
            INDENT();
            printf("blob(sbo, %u bytes): [ ", val->len);
            for (size_t i = 0; i < val->len; i++) {
                printf("%02x ", val->small_blob[i]);
            }
            printf("]\n");
            return ANYARR_OK;
        }

        case TYPE_PTR:
            INDENT();
            printf("ptr: %p\n", val->data.p);
            return ANYARR_OK;

        case TYPE_ARRAY: {
            INDENT();
            printf("[\n");
            AnyIter it = any_iter(val);
            ANY_NAMESPACE *item;
            while ((item = any_iter_next(&it))) {
                _any_print_impl(item, depth + 1);
            }
            INDENT();
            printf("]\n");
            return ANYARR_OK;
        }

        case TYPE_MAP: {
            INDENT();
            printf("{\n");
            AnyIter it = any_iter(val);
            ANY_NAMESPACE *item;
            while ((item = any_iter_next(&it))) {
                for (int _i = 0; _i < depth + 1; _i++) {
                    printf("  ");
                }
                _any_key_print(it.last_key);
                printf(":\n");
                _any_print_impl(item, depth + 2);
            }
            INDENT();
            printf("}\n");
            return ANYARR_OK;
        }

#undef INDENT
        default:
            return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
}

static inline anyarr_result _any_print_helper(ANY_NAMESPACE val) {
    return _any_print_impl(&val, 0);
}

#define any_print(x) _Generic((x),                                          \
    ANY_NAMESPACE*: _any_print_impl((ANY_NAMESPACE*)(x), 0),                \
    DynamicArray*: _any_print_helper(assign_array((DynamicArray*)(x))),     \
    HashMap*: _any_print_helper(assign_map((HashMap*)(x))),                 \
    default: _any_print_helper(assign_any(x))                               \
)


static inline uint64_t map_hash_str(const HashMap *map, const char *key) {
    static const uint64_t WY0 = 0xa0761d6478bd642full;
    static const uint64_t WY1 = 0xe7037ed1a0b428dbull;
    static const uint64_t WY2 = 0x3c6ef372fe94f82bull;
    static const uint64_t WY3 = 0xf1357aea2e62a9c5ull;
    const size_t len = strlen(key);
    const uint8_t *p = (const uint8_t *)key;
    uint64_t seed = map->hash_seed ^ WY0;
    uint64_t a, b;

    if (__builtin_expect(len <= 16, 1)) {
        if (__builtin_expect(len >= 4, 1)) {
            uint32_t lo0, lo1, hi0, hi1;
            memcpy(&lo0, p, 4);
            memcpy(&lo1, p + ((len >> 3) << 2), 4);
            memcpy(&hi0, p + len - 4, 4);
            memcpy(&hi1, p + len - 4 - ((len >> 3) << 2), 4);
            a = ((uint64_t)lo0 << 32) | (uint64_t)lo1;
            b = ((uint64_t)hi0 << 32) | (uint64_t)hi1;
        } else if (__builtin_expect(len > 0, 1)) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | (uint64_t)p[len - 1];
            b = 0;
        } else {
            a = 0; b = 0;
        }
    } else if (__builtin_expect(len <= 48, 1)) {
        uint64_t r0, r1, r2, r3;
        memcpy(&r0, p, 8);
        memcpy(&r1, p + 8, 8);
        __uint128_t m0 = (__uint128_t)(r0 ^ WY1) * (r1 ^ seed);
        seed = (uint64_t)(m0) ^ (uint64_t)(m0 >> 64);
        if (len > 32) {
            memcpy(&r2, p + 16, 8);
            memcpy(&r3, p + 24, 8);
            __uint128_t m1 = (__uint128_t)(r2 ^ WY2) * (r3 ^ seed);
            seed = (uint64_t)(m1) ^ (uint64_t)(m1 >> 64);
        }
        memcpy(&a, p + len - 16, 8);
        memcpy(&b, p + len - 8, 8);
    } else {
        size_t i = len;
        uint64_t s1 = seed, s2 = seed;
        while (__builtin_expect(i > 48, 1)) {
            uint64_t r0, r1, r2, r3, r4, r5;
            memcpy(&r0, p, 8);
            memcpy(&r1, p + 8, 8);
            memcpy(&r2, p + 16, 8);
            memcpy(&r3, p + 24, 8);
            memcpy(&r4, p + 32, 8);
            memcpy(&r5, p + 40, 8);
            __uint128_t m0 = (__uint128_t)(r0 ^ WY1) * (r1 ^ seed);
            seed = (uint64_t)(m0) ^ (uint64_t)(m0 >> 64);
            __uint128_t m1 = (__uint128_t)(r2 ^ WY2) * (r3 ^ s1);
            s1 = (uint64_t)(m1) ^ (uint64_t)(m1 >> 64);
            __uint128_t m2 = (__uint128_t)(r4 ^ WY3) * (r5 ^ s2);
            s2 = (uint64_t)(m2) ^ (uint64_t)(m2 >> 64);
            p += 48; i -= 48;
        }
        seed ^= s1 ^ s2;
        while (i > 16) {
            uint64_t r0, r1;
            memcpy(&r0, p, 8);
            memcpy(&r1, p + 8, 8);
            __uint128_t m0 = (__uint128_t)(r0 ^ WY1) * (r1 ^ seed);
            seed = (uint64_t)(m0) ^ (uint64_t)(m0 >> 64);
            p += 16; i -= 16;
        }
        memcpy(&a, p + i - 16, 8);
        memcpy(&b, p + i - 8, 8);
    }

    const __uint128_t final = (__uint128_t)(WY1 ^ len) * ((__uint128_t)(a ^ WY1) * (b ^ seed));
    return (uint64_t)(final) ^ (uint64_t)(final >> 64);
}


static inline uint64_t map_hash(const HashMap *map, const ANY_NAMESPACE *key) {
    static const uint64_t WY0 = 0xa0761d6478bd642full;
    static const uint64_t WY1 = 0xe7037ed1a0b428dbull;
    if (key->type == TYPE_STRING) {
        return map_hash_str(map, key->data.s);
    }
    if (key->type == TYPE_STRING_SMALL) {
        return map_hash_str(map, key->small_buf);
    }
    const uint64_t x = key->data.u ^ ((uint64_t)key->type << 56);
    const __uint128_t m = (__uint128_t)(x ^ WY0) * (map->hash_seed ^ WY1);
    return (uint64_t)(m) ^ (uint64_t)(m >> 64);
}


static inline _Bool map_key_equal(const ANY_NAMESPACE *a, const ANY_NAMESPACE *b) {
    const _Bool a_str = a->type == TYPE_STRING || a->type == TYPE_STRING_SMALL;
    const _Bool b_str = b->type == TYPE_STRING || b->type == TYPE_STRING_SMALL;
    if (a_str && b_str) {
        const char *sa = nullptr;
        if (a->type == TYPE_STRING) {
            sa = a->data.s;
        } else {
            sa = a->small_buf;
        }
        const char *sb = nullptr;
        if (b->type == TYPE_STRING) {
            sb = b->data.s;
        } else {
            sb = b->small_buf;
        }
        return strcmp(sa, sb) == 0;
    }
    if (a->type != b->type) {
        return false;
    }
    return a->data.u == b->data.u;
}


static inline void resize_memset(uint8_t *out, const size_t count) {
#if  defined(__AVX512BW__)
    const __m512i empty_vec = _mm512_set1_epi8((char)CTRL_EMPTY);
    const uint8_t *end = out + count;
    while ((uintptr_t)out & 63) *out++ = CTRL_EMPTY;
    while (out + 64 <= end) {
        _mm512_stream_si512((__m512i *)out, empty_vec);
        out += 64;
    }
    while (out < end) {
        *out++ = CTRL_EMPTY;
    }
    _mm_sfence();
#elif defined(__AVX2__)
    const __m256i empty_vec = _mm256_set1_epi8((char)CTRL_EMPTY);
    const uint8_t *end = out + count;
    while ((uintptr_t)out & 31) *out++ = CTRL_EMPTY;
    while (out + 32 <= end) {
        _mm256_stream_si256((__m256i *)out, empty_vec);
        out += 32;
    }
    while (out < end) {
        *out++ = CTRL_EMPTY;
    }
    _mm_sfence();
#else
    memset(out, CTRL_EMPTY, count);
#endif
}


static inline anyarr_result _map_resize_arena(HashMap *m, ARENA_NAMESPACE *arena) {
    const uint8_t *old_fingerprint = m->fingerprint;
    const Slots *old_slots = m->slots;
    const size_t old_capacity = m->capacity;
    size_t new_capacity = old_capacity;
    if (m->size * 2 >= old_capacity) {
        new_capacity = old_capacity << 1;
    }
    uint8_t *new_fingerprint = nullptr;
    Slots *new_slots = nullptr;
    arena_alloc_aligned64(arena, new_capacity * sizeof(uint8_t) + 64, (void **) &new_fingerprint);
    arena_alloc_aligned64(arena, new_capacity * sizeof(Slots), (void **) &new_slots);
    resize_memset(new_fingerprint, new_capacity + 64);
    m->fingerprint = new_fingerprint;
    m->slots = new_slots;
    m->capacity = new_capacity;
    m->size = 0;
    m->tombstone = 0;
#if defined(__AVX512BW__)
    const __m512i top_bit_vec = _mm512_set1_epi8((char)0x80);
    for (size_t i = 0; i < old_capacity; ) {
        const size_t remaining = old_capacity - i;
        __mmask64 load_mask;
        if (remaining >= 64) {
            load_mask = ~0ULL;
        } else {
            load_mask = (1ULL << remaining) - 1;
        }
        const __m512i chunk = _mm512_maskz_loadu_epi8(load_mask, old_fingerprint + i);
        const __m512i top_bits = _mm512_and_si512(chunk, top_bit_vec);
        const __mmask64 dead_bits = _mm512_mask_cmpeq_epi8_mask(load_mask, top_bits, top_bit_vec);
        uint64_t live_bits = (uint64_t)(load_mask & ~dead_bits);
        while (live_bits) {
            const uint64_t slot = _tzcnt_u64(live_bits);
            const size_t old_i = i + slot;
            size_t index = map_hash(m, &old_slots[old_i].key) & (m->capacity - 1);
            while (m->fingerprint[index] != CTRL_EMPTY) {
                index = (index + 1) & (m->capacity - 1);
            }
            m->slots[index] = old_slots[old_i];
            m->fingerprint[index] = old_fingerprint[old_i];
            m->size++;
            live_bits = _blsr_u64(live_bits);
        }
        if (remaining >= 64) {
            i += 64;
        } else {
            i += remaining;
        }
    }
#elif defined(__AVX2__)
    const __m256i top_bit_vec = _mm256_set1_epi8((char)0x80);
    for (size_t i = 0; i < old_capacity; ) {
        const size_t remaining = old_capacity - i;
        uint32_t load_mask;
        if (remaining >= 32) {
            load_mask = ~0U;
        } else {
            load_mask = (1U << remaining) - 1;
        }
        const __m256i chunk = _mm256_loadu_si256((const __m256i *)(old_fingerprint + i));
        const __m256i top_bits = _mm256_and_si256(chunk, top_bit_vec);
        const __m256i is_dead = _mm256_cmpeq_epi8(top_bits, top_bit_vec);
        uint32_t dead_bits = (uint32_t)_mm256_movemask_epi8(is_dead) | ~load_mask;
        uint32_t live_bits = ~dead_bits;
        while (live_bits) {
            const uint64_t slot = _tzcnt_u32(live_bits);
            const size_t old_i = i + slot;
            size_t index = map_hash(m, &old_slots[old_i].key) & (m->capacity - 1);
            while (m->fingerprint[index] != CTRL_EMPTY) {
                index = (index + 1) & (m->capacity - 1);
            }
            m->slots[index] = old_slots[old_i];
            m->fingerprint[index] = old_fingerprint[old_i];
            m->size++;
            live_bits = _blsr_u32(live_bits);
        }
        if (remaining >= 32) {
            i += 32;
        } else {
            i += remaining;
        }
    }
#else
    for (size_t i = 0; i < old_capacity; i++) {
        const uint8_t ctrl = old_fingerprint[i];
        if (ctrl < 0x80) {
            size_t index = map_hash(m, &old_slots[i].key) & (m->capacity - 1);
            while (m->fingerprint[index] != CTRL_EMPTY) {
                index = (index + 1) & (m->capacity - 1);
            }
            m->slots[index] = old_slots[i];
            m->fingerprint[index] = ctrl;
            m->size++;
        }
    }
#endif
    return ANYARR_OK;
}


static inline anyarr_result _map_resize_impl(HashMap *m) {
    return _map_resize_arena(m, ARENA_CTX);
}

#define map_resize(...) ANYARR_ARG2(__VA_ARGS__, _map_resize_arena, _map_resize_impl) (__VA_ARGS__)


static inline void _map_reserve_arena(HashMap *m, const size_t count, ARENA_NAMESPACE *arena) {
    size_t needed = (count * 8 + 6) / 7;
    size_t cap = 64;
    while (cap < needed) {
        cap <<= 1;
    }
    if (cap <= m->capacity) {
        return;
    }
    const size_t old_cap = m->capacity;
    const uint8_t *old_fp = m->fingerprint;
    const Slots *old_slots = m->slots;
    uint8_t *new_fp = nullptr;
    Slots *new_slots = nullptr;
    arena_alloc_aligned64(arena, cap * sizeof(uint8_t) + 64, (void **) &new_fp);
    arena_alloc_aligned64(arena, cap * sizeof(Slots), (void **) &new_slots);
    resize_memset(new_fp, cap + 64);
    m->fingerprint = new_fp;
    m->slots = new_slots;
    m->capacity = cap;
    const size_t old_size = m->size;
    m->size = 0;
    m->tombstone = 0;
    for (size_t i = 0; i < old_cap; i++) {
        const uint8_t ctrl = old_fp[i];
        if (ctrl < 0x80) {
            size_t index = map_hash(m, &old_slots[i].key) & (m->capacity - 1);
            while (m->fingerprint[index] != CTRL_EMPTY) {
                index = (index + 1) & (m->capacity - 1);
            }
            m->slots[index] = old_slots[i];
            m->fingerprint[index] = ctrl;
            m->size++;
        }
    }
    (void) old_size;
}

static inline void _map_reserve_impl(HashMap *m, const size_t count) {
    _map_reserve_arena(m, count, ARENA_CTX);
}

#define map_reserve(...) ANYARR_ARG3(__VA_ARGS__, _map_reserve_arena, _map_reserve_impl) (__VA_ARGS__)


static inline anyarr_result _map_get_impl(const HashMap *m, const ANY_NAMESPACE key, ANY_NAMESPACE **out_value, const _Bool is_internal) {
    if (m == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    const uint64_t hash = map_hash(m, &key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;  // There's a 1/255 chance that the fingerprint itself can store 0xFF or 0xFE as value so we truncate the first bit so it never reaches that range
    size_t index = hash & (m->capacity - 1);
    __builtin_prefetch(&m->slots[index], 0, 1);
#if defined(__AVX512BW__)
    const __m512i fingerprint_vec = _mm512_set1_epi8((char)fingerprint);
    const __m512i empty_vec = _mm512_set1_epi8((char)CTRL_EMPTY);
    size_t base = index & ~(size_t)63;
    uint64_t head_mask = ~0ULL << (index - base);
    while (true) {
        const __m512i chunk = _mm512_load_si512((const void *) &m->fingerprint[base]);
        const uint64_t empty_bits = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, empty_vec) & head_mask;
        uint64_t hits = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, fingerprint_vec) & head_mask;
        while (hits) {
            const uint64_t slot = _tzcnt_u64(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                *out_value = &m->slots[i].value;
                return ANYARR_OK;
            }
            hits = _blsr_u64(hits);
        }
        if (empty_bits != 0) {
            if (!is_internal) {
                return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
            } else {
                return ANYARR_ERR_EMPTY_KEY;
            }
        }
        base = (base + 64) & (m->capacity - 1);
        head_mask = ~0ULL;
    }
#elif defined(__AVX2__)
    const __m256i fingerprint_vec = _mm256_set1_epi8((char)fingerprint);
    const __m256i empty_vec = _mm256_set1_epi8((char)CTRL_EMPTY);
    size_t base = index & ~(size_t)31;
    uint32_t head_mask = ~0U << (index - base);
    while (true) {
        const __m256i chunk = _mm256_load_si256((const __m256i *) &m->fingerprint[base]);
        const uint32_t empty_bits = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, empty_vec)) & head_mask;
        uint32_t hits = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, fingerprint_vec)) & head_mask;
        while (hits) {
            const uint64_t slot = _tzcnt_u32(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                *out_value = &m->slots[i].value;
                return ANYARR_OK;
            }
            hits = _blsr_u32(hits);
        }
        if (empty_bits != 0) {
            if (!is_internal) {
                return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
            } else {
                return ANYARR_ERR_EMPTY_KEY;
            }
        }
        base = (base + 32) & (m->capacity - 1);
        head_mask = ~0U;
    }
#else
    while (true) {
        const uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            if (!is_internal) {
                return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
            } else {
                return ANYARR_ERR_EMPTY_KEY;
            }
        }
        if (ctrl == fingerprint && map_key_equal(&m->slots[index].key, &key)) {
            *out_value = &m->slots[index].value;
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
#endif
}


// Internal and only returns value instead of printing to stderr, takes an already built Any key
static inline anyarr_result _map_get_silent(const HashMap *m, const ANY_NAMESPACE key, ANY_NAMESPACE **out_value) {
    return _map_get_impl(m, key, out_value, true);
}

#define map_get(m, key, out_value) _map_get_impl((m), _lookup_key(key), (out_value), false)
#define map_get_silent(m, key, out_value) _map_get_impl((m), _lookup_key(key), (out_value), true)


static inline void _map_put_arena(HashMap* m, const ANY_NAMESPACE key, const ANY_NAMESPACE value, ARENA_NAMESPACE *arena) {
    if (m == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if ((m->size + m->tombstone + 1) * 8 >= m->capacity * 7) {
        map_resize(m, arena);
    }
    const uint64_t hash = map_hash(m, &key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;
    size_t index = hash & (m->capacity - 1);
    __builtin_prefetch(&m->slots[index], 1, 1);
    size_t first_tombstone = SIZE_MAX;
#if defined(__AVX512BW__)
    const __m512i fingerprint_vec = _mm512_set1_epi8((char)fingerprint);
    const __m512i empty_vec = _mm512_set1_epi8((char)CTRL_EMPTY);
    const __m512i deleted_vec = _mm512_set1_epi8((char)CTRL_DELETED);
    size_t base = index & ~(size_t)63;
    uint64_t head_mask = ~0ULL << (index - base);
    while (true) {
        const __m512i chunk = _mm512_load_si512((const void *) &m->fingerprint[base]);
        const uint64_t fp_bits      = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, fingerprint_vec) & head_mask;
        const uint64_t empty_bits   = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, empty_vec) & head_mask;
        const uint64_t deleted_bits = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, deleted_vec) & head_mask;

        uint64_t hits = fp_bits;
        while (hits) {
            const uint64_t slot = _tzcnt_u64(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                m->slots[i].value = value;
                return;
            }
            hits = _blsr_u64(hits);
        }

        if (first_tombstone == SIZE_MAX && deleted_bits) {
            uint64_t valid_deleted = deleted_bits;
            if (empty_bits) {
                const uint64_t empty_lsb = empty_bits & (~empty_bits + 1);
                valid_deleted &= (empty_lsb - 1);
            }
            if (valid_deleted) {
                const uint64_t slot = _tzcnt_u64(valid_deleted);
                first_tombstone = base + slot;
            }
        }

        if (empty_bits) {
            const uint64_t slot = _tzcnt_u64(empty_bits);
            size_t insert_at = base + slot;
            if (first_tombstone != SIZE_MAX) {
                insert_at = first_tombstone;
                m->tombstone--;
            }
            m->slots[insert_at].key = key;
            m->slots[insert_at].value = value;
            m->fingerprint[insert_at] = fingerprint;
            m->size++;
            return;
        }

        base = (base + 64) & (m->capacity - 1);
        head_mask = ~0ULL;
    }
#elif defined(__AVX2__)
    const __m256i fingerprint_vec = _mm256_set1_epi8((char)fingerprint);
    const __m256i empty_vec = _mm256_set1_epi8((char)CTRL_EMPTY);
    const __m256i deleted_vec = _mm256_set1_epi8((char)CTRL_DELETED);
    size_t base = index & ~(size_t)31;
    uint32_t head_mask = ~0U << (index - base);
    while (true) {
        const __m256i chunk = _mm256_load_si256((const __m256i *) &m->fingerprint[base]);
        const uint32_t fp_bits      = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, fingerprint_vec)) & head_mask;
        const uint32_t empty_bits   = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, empty_vec)) & head_mask;
        const uint32_t deleted_bits = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, deleted_vec)) & head_mask;

        uint32_t hits = fp_bits;
        while (hits) {
            const uint64_t slot = _tzcnt_u32(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                m->slots[i].value = value;
                return;
            }
            hits = _blsr_u32(hits);
        }

        if (first_tombstone == SIZE_MAX && deleted_bits) {
            uint32_t valid_deleted = deleted_bits;
            if (empty_bits) {
                const uint32_t empty_lsb = empty_bits & (~empty_bits + 1);
                valid_deleted &= (empty_lsb - 1);
            }
            if (valid_deleted) {
                const uint64_t slot = _tzcnt_u32(valid_deleted);
                first_tombstone = base + slot;
            }
        }

        if (empty_bits) {
            const uint64_t slot = _tzcnt_u32(empty_bits);
            size_t insert_at = base + slot;
            if (first_tombstone != SIZE_MAX) {
                insert_at = first_tombstone;
                m->tombstone--;
            }
            m->slots[insert_at].key = key;
            m->slots[insert_at].value = value;
            m->fingerprint[insert_at] = fingerprint;
            m->size++;
            return;
        }

        base = (base + 32) & (m->capacity - 1);
        head_mask = ~0U;
    }
#else
    while (true) {
        const uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            size_t insert_at = index;
            if (first_tombstone != SIZE_MAX) {
                insert_at = first_tombstone;
                m->tombstone--;
            }
            m->slots[insert_at].key = key;
            m->slots[insert_at].value = value;
            m->fingerprint[insert_at] = fingerprint;
            m->size++;
            return;
        }
        if (ctrl == CTRL_DELETED) {
            if (first_tombstone == SIZE_MAX) {
                first_tombstone = index;
            }
        } else if (ctrl == fingerprint && map_key_equal(&m->slots[index].key, &key)) {
            m->slots[index].value = value;
            return;
        }
        index = (index + 1) & (m->capacity - 1);
    }
#endif
}


static inline void _map_put_impl(HashMap *m, const ANY_NAMESPACE key, const ANY_NAMESPACE value) {
    _map_put_arena(m, key, value, ARENA_CTX);
}

#define _map_put_impl_any(m, key, value) _map_put_impl(m, assign_key(key), assign_any(value))
#define _map_put_arena_any(m, key, value, arena) _map_put_arena(m , assign_key(key, arena), assign_any(value, arena), arena)

#define map_put(...) ANYARR_ARG4(__VA_ARGS__, _map_put_arena_any, _map_put_impl_any) (__VA_ARGS__)


static inline anyarr_result _map_remove(HashMap *m, const ANY_NAMESPACE key) {
    if (m == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    const uint64_t hash = map_hash(m, &key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;
    size_t index = hash & (m->capacity - 1);
    __builtin_prefetch(&m->slots[index], 0, 1);
#if defined(__AVX512BW__)
    const __m512i fingerprint_vec = _mm512_set1_epi8((char)fingerprint);
    const __m512i empty_vec = _mm512_set1_epi8((char)CTRL_EMPTY);
    size_t base = index & ~(size_t)63;
    uint64_t head_mask = ~0ULL << (index - base);
    while (true) {
        const __m512i chunk = _mm512_load_si512((const void *) &m->fingerprint[base]);
        const uint64_t empties = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, empty_vec) & head_mask;
        uint64_t hits = (uint64_t)_mm512_cmpeq_epi8_mask(chunk, fingerprint_vec) & head_mask;
        while (hits) {
            const uint64_t slot = _tzcnt_u64(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                m->fingerprint[i] = CTRL_DELETED;
                m->size--;
                m->tombstone++;
                return ANYARR_OK;
            }
            hits = _blsr_u64(hits);
        }
        if (empties != 0) {
            return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
        }
        base = (base + 64) & (m->capacity - 1);
        head_mask = ~0ULL;
    }
#elif defined(__AVX2__)
    const __m256i fingerprint_vec = _mm256_set1_epi8((char)fingerprint);
    const __m256i empty_vec = _mm256_set1_epi8((char)CTRL_EMPTY);
    size_t base = index & ~(size_t)31;
    uint32_t head_mask = ~0U << (index - base);
    while (true) {
        const __m256i chunk = _mm256_load_si256((const __m256i *) &m->fingerprint[base]);
        const uint32_t empties = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, empty_vec)) & head_mask;
        uint32_t hits = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, fingerprint_vec)) & head_mask;
        while (hits) {
            const uint64_t slot = _tzcnt_u32(hits);
            const size_t i = base + slot;
            if (map_key_equal(&m->slots[i].key, &key)) {
                m->fingerprint[i] = CTRL_DELETED;
                m->size--;
                m->tombstone++;
                return ANYARR_OK;
            }
            hits = _blsr_u32(hits);
        }
        if (empties != 0) {
            return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
        }
        base = (base + 32) & (m->capacity - 1);
        head_mask = ~0U;
    }
#else
    while (true) {
        const uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            return handle_error(ANYARR_ERR_EMPTY_KEY, __LINE__, __FILE__);
        }
        if (ctrl == fingerprint && map_key_equal(&m->slots[index].key, &key)) {
            m->fingerprint[index] = CTRL_DELETED;
            m->size--;
            m->tombstone++;
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
#endif
}

#define map_remove(m, key) _map_remove((m), _lookup_key(key))


static inline void array_copy(ANY_NAMESPACE *out, ANY_NAMESPACE *in, const size_t count) {
    const size_t bytes = count * sizeof(ANY_NAMESPACE);
    if (bytes < ANYARR_L2_SIZE) { // Optimization, change macro value depending on targeted cpu
        memcpy(out, in, bytes);
        return;
    }
#if defined(__AVX512BW__)
    const ANY_NAMESPACE *end = in + count;
    while ((uintptr_t)out & 63) {
        *out++ = *in++;
    }
    while (in + 4 <= end) {
        __builtin_prefetch(in + ANYARR_PREFETCH_DISTANCE * 4, 0, 0);
        const __m512i chunk = _mm512_loadu_si512(in);
        _mm512_stream_si512((__m512i *)out, chunk);
        out += 4;
        in += 4;
    }
    while (in < end) {
        *out++ = *in++;
    }
    _mm_sfence();
#elif defined(__AVX2__)
    const ANY_NAMESPACE *end = in + count;
    while ((uintptr_t)out & 31) {
        *out++ = *in++;
    }
    while (in + 2 <= end) {
        __builtin_prefetch(in + ANYARR_PREFETCH_DISTANCE * 2, 0, 0);
        const __m256i chunk = _mm256_loadu_si256((__m256i *)in);
        _mm256_stream_si256((__m256i *)out, chunk);
        out += 2;
        in += 2;
    }
    if (in < end) {
        *out = *in;
    }
    _mm_sfence();
#else
    memcpy(out, in, bytes);
#endif
}


static inline anyarr_result _array_append_arena(DynamicArray *buf, const ANY_NAMESPACE value, ARENA_NAMESPACE *arena) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (buf->size == buf->capacity) {
        size_t new_capacity = 4;
        if (buf->capacity != 0) {
            new_capacity = buf->capacity + (buf->capacity >> 1);
        }
        const size_t old_bytes = arena_align_up(buf->capacity * sizeof(ANY_NAMESPACE));
        const size_t new_bytes = arena_align_up(new_capacity * sizeof(ANY_NAMESPACE));
        const _Bool at_tip = (buf->data != nullptr) && ((uint8_t *) buf->data + old_bytes == arena->base + arena->used);
        if (at_tip) {
            const size_t extra = new_bytes - old_bytes;
            const size_t new_used = arena->used + extra;
            if (new_used > arena->reserved) {
                return handle_error(ANYARR_ERR_OOM, __LINE__, __FILE__);
            }
            if (new_used > arena->committed) {
                arena_commit(arena, new_used - arena->committed);
            }
            arena->used = new_used;
            buf->capacity = new_capacity;
        } else {
            ANY_NAMESPACE *temp;
            arena_alloc(arena, new_capacity * sizeof(ANY_NAMESPACE), (void **) &temp);
            if (buf->size > 0) {
                array_copy(temp, buf->data, buf->size);
            }
            buf->data = temp;
            buf->capacity = new_capacity;
        }
    }
    buf->data[buf->size++] = value;
    return ANYARR_OK;
}


static inline anyarr_result _array_append_impl(DynamicArray *buf, const ANY_NAMESPACE value) {
    return _array_append_arena(buf, value, ARENA_CTX);
}

#define _array_append_impl_any(buf, value) _array_append_impl(buf, assign_any(value))
#define _array_append_arena_any(buf, value, arena) _array_append_arena(buf, assign_any(value, arena), arena)

#define array_append(...) ANYARR_ARG3(__VA_ARGS__, _array_append_arena_any, _array_append_impl_any) (__VA_ARGS__)


static inline anyarr_result array_remove_index(DynamicArray *buf, const size_t index) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
    }
    const size_t index_queue = buf->size - index - 1;
    if (index_queue > 0) {
        memmove(&buf->data[index], &buf->data[index + 1], index_queue * sizeof(ANY_NAMESPACE));
    }
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result _array_set_index_impl(const DynamicArray *buf, const size_t index, const ANY_NAMESPACE value) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
    }
    buf->data[index] = value;
    return ANYARR_OK;
}
#define array_set_index(buf, index, value) _array_set_index_impl(buf, index, assign_any(value))


static inline anyarr_result array_get(const DynamicArray *buf, const size_t index, ANY_NAMESPACE **out_value) {
    if (buf == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
    }

    *out_value = &buf->data[index];
    return ANYARR_OK;
}


static inline anyarr_result any_get_path(ANY_NAMESPACE *root, const char *path, ANY_NAMESPACE **out_value) {
    if (root == nullptr || path == nullptr || out_value == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    ANY_NAMESPACE *current = root;
    const char *p = path;
    char segment[256]; // Not worth making it arena allocated
    while (*p != '\0') {
        while (*p == '.' || *p == '[' || *p == ']') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        size_t i = 0;
        while (*p != '\0' && *p != '.' && *p != '[' && *p != ']') {
            if (i >= sizeof(segment) - 1) {
                return handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
            }
            segment[i++] = *p++;
        }
        segment[i] = '\0';
        if (current->type == TYPE_MAP) {
            const anyarr_result r = _map_get_silent(current->data.m, _lookup_key(segment), &current);
            if (r != ANYARR_OK) {
                return handle_error(r, __LINE__, __FILE__);
            }
        } else if (current->type == TYPE_ARRAY) {
            char *end_ptr;
            const size_t index = strtoull(segment, &end_ptr, 10);
            if (*end_ptr != '\0') {
                return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
            }
            const anyarr_result r = array_get(current->data.a, index, &current);
            if (r != ANYARR_OK) {
                return r;
            }
        } else {
            return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
        }
    }
    *out_value = current;
    return ANYARR_OK;
}


static inline void _array_reserve_arena(DynamicArray *buf, const size_t new_capacity, ARENA_NAMESPACE *arena) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (new_capacity <= buf->capacity) {
        return;
    }
    ANY_NAMESPACE *temp;
    arena_alloc(arena, new_capacity * sizeof(ANY_NAMESPACE), (void **) &temp);
    if (buf->size > 0 && buf->data != nullptr) {
        memcpy(temp, buf->data, buf->size * sizeof(ANY_NAMESPACE));
    }
    buf->data = temp;
    buf->capacity = new_capacity;
}


static inline void _array_reserve_impl(DynamicArray *buf, const size_t new_capacity) {
    _array_reserve_arena(buf, new_capacity, ARENA_CTX);
}

#define array_reserve(...) ANYARR_ARG3(__VA_ARGS__, _array_reserve_arena, _array_reserve_impl) (__VA_ARGS__)


static inline anyarr_result _any_clone_arena(const ANY_NAMESPACE *src, ANY_NAMESPACE *dest, ARENA_NAMESPACE *arena) {
    if (src == nullptr || dest == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    switch (src->type) {
        case TYPE_NULL:
        case TYPE_BOOL:
        case TYPE_CHAR:
        case TYPE_INT:
        case TYPE_UINT:
        case TYPE_FLOAT:
        case TYPE_DOUBLE:
        case TYPE_STRING_SMALL:
        case TYPE_BLOB_SMALL:
            *dest = *src;
            return ANYARR_OK;
        case TYPE_STRING:
            *dest = assign_string(src->data.s, arena);
            return ANYARR_OK;
        case TYPE_ARRAY: {
            const DynamicArray *src_arr = src->data.a;
            DynamicArray *new_arr;
            arena_alloc(arena, sizeof(DynamicArray), (void **) &new_arr);
            array_init(new_arr, arena);
            for (size_t i = 0; i < src_arr->size; i++) {
                ANY_NAMESPACE cloned_elem;
                const anyarr_result res = _any_clone_arena(&src_arr->data[i], &cloned_elem, arena);
                if (res != ANYARR_OK) {
                    return res;
                }
                _array_append_arena(new_arr, cloned_elem, arena);
            }
            *dest = assign_array(new_arr);
            return ANYARR_OK;
        }
        case TYPE_PTR:
            *dest = assign_ptr(src->data.p);
            return ANYARR_OK;
        case TYPE_BLOB: {
            Blob b;
            any_get_blob(src, &b);
            *dest = assign_blob(&b, arena);
            return ANYARR_OK;
        }
        case TYPE_MAP: {
            const HashMap *src_map = src->data.m;
            HashMap *new_map;
            arena_alloc(arena, sizeof(HashMap), (void **) &new_map);
            map_init(new_map, arena);
            for (size_t i = 0; i < src_map->capacity; i++) {
                const uint8_t ctrl = src_map->fingerprint[i];
                if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
                    continue;
                }
                ANY_NAMESPACE cloned_val;
                const anyarr_result res = _any_clone_arena(&src_map->slots[i].value, &cloned_val, arena);
                if (res != ANYARR_OK) {
                    return res;
                }
                ANY_NAMESPACE cloned_key;
                const anyarr_result key_res = _any_clone_arena(&src_map->slots[i].key, &cloned_key, arena);
                if (key_res != ANYARR_OK) {
                    return key_res;
                }
                _map_put_arena(new_map, cloned_key, cloned_val, arena);
            }
            *dest = assign_map(new_map, arena);
            return ANYARR_OK;
        }
        default:
            break;
    }
    return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
}


static inline anyarr_result _any_clone_impl(const ANY_NAMESPACE *src, ANY_NAMESPACE *dest) {
    return _any_clone_arena(src, dest, ARENA_CTX);
}

#define any_clone(...) ANYARR_ARG3(__VA_ARGS__, _any_clone_arena, _any_clone_impl) (__VA_ARGS__)


static inline anyarr_result any_equal(const ANY_NAMESPACE *a, const ANY_NAMESPACE *b) {
    if (a == nullptr || b == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if ((a->type == TYPE_STRING || a->type == TYPE_STRING_SMALL) && (
            b->type == TYPE_STRING || b->type == TYPE_STRING_SMALL)) {
        const char *sa, *sb;
        any_get_string(a, &sa);
        any_get_string(b, &sb);
        if (strcmp(sa, sb) == 0) {
            return ANYARR_EQUAL;
        }
        return ANYARR_NOT_EQUAL;
    }

    if ((a->type == TYPE_BLOB || a->type == TYPE_BLOB_SMALL) && (
        b->type == TYPE_BLOB || b->type == TYPE_BLOB_SMALL)) {
        Blob ba, bb;
        any_get_blob(a, &ba);
        any_get_blob(b, &bb);
        if (ba.size != bb.size) {
            return ANYARR_NOT_EQUAL;
        }
        if (memcmp(ba.ptr, bb.ptr, ba.size) == 0) {
            return ANYARR_EQUAL;
        }
        return ANYARR_NOT_EQUAL;
    }

    if (a->type != b->type) {
        return ANYARR_NOT_EQUAL;
    }

    switch (a->type) {
        case TYPE_NULL:
            return ANYARR_EQUAL;
        case TYPE_BOOL:
            if (a->data.b == b->data.b) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_CHAR:
            if (a->data.c == b->data.c) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_INT:
            if (a->data.i == b->data.i) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_UINT:
            if (a->data.u == b->data.u) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_FLOAT:
            if (a->data.f == b->data.f) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_DOUBLE:
            if (a->data.d == b->data.d) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_PTR:
            if (a->data.p == b->data.p) {
                return ANYARR_EQUAL;
            }
            return ANYARR_NOT_EQUAL;
        case TYPE_ARRAY: {
            const DynamicArray *aa = a->data.a;
            const DynamicArray *ab = b->data.a;
            if (aa->size != ab->size) {
                return ANYARR_NOT_EQUAL;
            }
            for (size_t i = 0; i < aa->size; i++) {
                const anyarr_result res = any_equal(&aa->data[i], &ab->data[i]);
                if (res != ANYARR_EQUAL) {
                    return res;
                }
            }
            return ANYARR_EQUAL;
        }
        case TYPE_MAP: {
            const HashMap *ma = a->data.m;
            const HashMap *mb = b->data.m;
            if (ma->size != mb->size) {
                return ANYARR_NOT_EQUAL;
            }
            for (size_t i = 0; i < ma->capacity; i++) {
                const uint8_t ctrl = ma->fingerprint[i];
                if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
                    continue;
                }
                ANY_NAMESPACE *val_b;
                if (_map_get_silent(mb, ma->slots[i].key, &val_b) != ANYARR_OK) {
                    return ANYARR_NOT_EQUAL;
                }
                const anyarr_result res = any_equal(&ma->slots[i].value, val_b);
                if (res != ANYARR_EQUAL) {
                    return res;
                }
            }
            return ANYARR_EQUAL;
        }
        default:
            return handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
    }
}


static inline anyarr_result array_pop(DynamicArray *buf) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    if (buf->size == 0) {
        return handle_error(ANYARR_ERR_EMPTY, __LINE__, __FILE__);
    }
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result array_clear(DynamicArray *buf) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
    }
    buf->size = 0;
    return ANYARR_OK;
}


static inline const ANY_NAMESPACE *array_at(const DynamicArray *buf, size_t idx) {
    if (buf == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
        return nullptr;
    }
    if (idx >= buf->size) {
        handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
        return nullptr;
    }
    return &buf->data[idx];
}


static inline AnyIter any_iter(const ANY_NAMESPACE *root) {
    AnyIter it = {0};
    if (root == nullptr) {
        return it;
    }
    if (root->type == TYPE_ARRAY) {
        it.type = TYPE_ARRAY;
        it.data = root->data.a->data;
        it.bound = root->data.a->size;
    } else if (root->type == TYPE_MAP) {
        it.type = TYPE_MAP;
        it.fingerprint = root->data.m->fingerprint;
        it.slots = root->data.m->slots;
        it.bound = root->data.m->capacity;
    }
    return it;
}


static inline ANY_NAMESPACE *any_iter_next(AnyIter *it) {
    if (it->type == TYPE_ARRAY) {
        if (it->index < it->bound) {
            return &it->data[it->index++];
        }
        return nullptr;
    }

    if (it->type == TYPE_MAP) {
        while (it->index < it->bound) {
            const uint8_t ctrl = it->fingerprint[it->index];
            if (ctrl >= 0x80) {
                it->index++;
                continue;
            }
            it->last_key = &it->slots[it->index].key;
            return &it->slots[it->index++].value;
        }
        return nullptr;
    }
    return nullptr;
}


typedef struct {
    AnyIter stack[ANYARR_WALKER_DEPTH];
    int depth;
    int max_depth;
    anyarr_result result;
} AnyWalker;


static inline AnyWalker any_walker(const ANY_NAMESPACE *root, const int max_depth) {
    AnyWalker walk = {0};
    walk.depth = -1;
    walk.max_depth = max_depth;
    if (root == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
        return walk;
    }
    if (root->type != TYPE_ARRAY && root->type != TYPE_MAP) {
        handle_error(ANYARR_ERR_TYPE_MISMATCH, __LINE__, __FILE__);
        return walk;
    }
    walk.stack[0] = any_iter(root);
    walk.depth = 0;
    walk.result = ANYARR_OK;
    return walk;
}


static inline const ANY_NAMESPACE *any_walker_key(const AnyWalker *walk) {
    if (walk == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
        return nullptr;
    }
    if (walk->result != ANYARR_OK) {
        handle_error(walk->result, __LINE__, __FILE__);
        return nullptr;
    }
    if (walk->depth < 0) {
        return nullptr;
    }
    return walk->stack[walk->depth].last_key;
}


static inline ANY_NAMESPACE *any_walk_next(AnyWalker *walk) {
    if (walk == nullptr) {
        handle_error(ANYARR_ERR_NULLPTR, __LINE__, __FILE__);
        return nullptr;
    }
    if (walk->result != ANYARR_OK) {
        handle_error(walk->result, __LINE__, __FILE__);
        return nullptr;
    }
    if (walk->depth < 0) {
        return nullptr;
    }
    while (walk->depth >= 0) {
        ANY_NAMESPACE *val = any_iter_next(&walk->stack[walk->depth]);
        if (val == nullptr) {
            walk->depth--;
            continue;
        }
        const _Bool is_container = __builtin_expect(val->type == TYPE_ARRAY || val->type == TYPE_MAP, 0);
        if (!is_container) {
            return val;
        }
        const int next_depth = walk->depth + 1;
        const _Bool at_limit = (walk->max_depth != WALK_DEEP) && (next_depth >= walk->max_depth);
        if (at_limit) {
            return val;
        }
        if (next_depth >= ANYARR_WALKER_DEPTH) {
            walk->result = ANYARR_ERR_OUT_OF_BOUNDS;
            handle_error(ANYARR_ERR_OUT_OF_BOUNDS, __LINE__, __FILE__);
            return nullptr;
        }
        walk->stack[next_depth] = any_iter(val);
        walk->depth = next_depth;
    }
    return nullptr;
}


#define get_any(val_ptr, out_ptr) _Generic((out_ptr),   \
    _Bool*: any_get_bool,                               \
    char*: any_get_char,                                \
    int64_t*: any_get_int,                              \
    uint64_t*: any_get_uint,                            \
    float*: any_get_float,                              \
    double*: any_get_double,                            \
    const char**: any_get_string,                       \
    Blob*: any_get_blob,                                \
    void**: any_get_ptr                                 \
)(val_ptr, out_ptr)

// This is where it gets messy, it's a hacky macro which takes in the struct and uses the _Generic to assign DynamicArray and HashMap structure and iterates any_get_path
#define path_get(root, path, out_ptr)                                      \
get_any(({                                                                 \
    ANY_NAMESPACE *_v = nullptr;                                           \
    ANY_NAMESPACE _tmp;                                                    \
    ANY_NAMESPACE *_root = _Generic((root),                                \
    ANY_NAMESPACE*: (ANY_NAMESPACE*)(root),                                \
    DynamicArray*: (_tmp = assign_array((DynamicArray*)(root)), &_tmp),    \
    HashMap*: (_tmp = assign_map((HashMap*)(root)), &_tmp)                 \
);                                                                         \
    any_get_path(_root, (path), &_v);                                      \
    _v;                                                                    \
}), (out_ptr))

// Internal macro to handle iteration of HashMap and DynamicArray
#define _any_iter_generic(root_ptr) _Generic((root_ptr),                                          \
    ANY_NAMESPACE*: any_iter((ANY_NAMESPACE*)(root_ptr)),                                         \
    DynamicArray*: any_iter(&(ANY_NAMESPACE){TYPE_ARRAY, .data.a = (DynamicArray*)(root_ptr)}),   \
    HashMap*: any_iter(&(ANY_NAMESPACE){TYPE_MAP,   .data.m = (HashMap*)(root_ptr)})              \
)

#define _foreach_impl(item, root_ptr, ctr)              \
for (AnyIter _it_##ctr = _any_iter_generic(root_ptr);   \
(item = any_iter_next(&_it_##ctr)) != nullptr; )

#define _foreach_uniq(item, root_ptr, ctr) _foreach_impl(item, root_ptr, ctr)
#define foreach(item, root_ptr) _foreach_uniq(item, root_ptr, __COUNTER__)


#define _foreach_kv_impl(key, item, root_ptr, ctr)                      \
for (AnyIter _it_##ctr = _any_iter_generic(root_ptr);                   \
(item = any_iter_next(&_it_##ctr)) && (key = _it_##ctr.last_key, 1); )

#define _foreach_kv_uniq(key, item, root_ptr, ctr) _foreach_kv_impl(key, item, root_ptr, ctr)
#define foreach_kv(key, item, root_ptr) _foreach_kv_uniq(key, item, root_ptr, __COUNTER__)

#pragma GCC diagnostic pop
#endif
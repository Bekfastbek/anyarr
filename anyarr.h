/*
 * This library should work with GCC/Clang C11 onwards flawlessly but C99 needs a lot of setup to work and generally not recommended
 * To make it work on C99, you might need to use the GNU C99 standard instead of strict C99 for anonymous structs to work
 * There is also no support for generic macros so remove the macros at the bottom of the file and stick with specific datatypes
 * Another note, cleanup attribute is only a part of GCC/Clang not a part of C standard so it won't work with other compilers
 * Also this library is made to be as fast as possible while being easy to use so it's not exactly efficient on memory usage
 * As of now this library does not guarantee thread safety and documentation is in the README of the repo
 * It only works on MinGW on Windows and not at all on MSVC
*/

#ifndef ANYARR_H
#define ANYARR_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// The true/false values in stdbool get converted into int types so the hacky solution I thought is to undef and explicitly define true/false as (_Bool) types so assign_any doesn't confuse them with int
#ifndef bool
#define bool _Bool
#endif
#undef true
#undef false
#define true  ((_Bool)1)
#define false ((_Bool)0)

/*TO DO:
 * SIMD: x86 AVX512, AVX2 and ARM64 NEON (Apple doesn't support SVE)
 * SIMD Improvements: any_equal, arena_restore
 * NumArray: typed SIMD array (double), Int64Array (int64_t exact integers) but the priority is the double array and let it also convert integers into double
 * _Thread_local arenas with a thread pool
 */

#pragma GCC diagnostic ignored "-Wunused-function"

#if defined(_WIN32) || defined(_WIN64)
#   define ANYARR_PLATFORM_WINDOWS
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#else
#   define ANYARR_PLATFORM_POSIX
#   include <sys/mman.h>
#   ifndef MAP_ANONYMOUS
#       define MAP_ANONYMOUS MAP_ANON
#   endif
#endif

#ifndef ANYARR_RESERVE_SIZE
#   ifdef ANYARR_PLATFORM_WINDOWS
#       define ANYARR_RESERVE_SIZE (8ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)
#   else
#       define ANYARR_RESERVE_SIZE (128ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)
#   endif
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

#define WALK_SHALLOW 1
#define WALK_DEEP    0


typedef enum {
    ANYARR_OK = 0,
    ANYARR_EQUAL = 1,
    ANYARR_NOT_EQUAL = 2,
    // These would be also assuming the type of TYPE_INT when casting to ANY_NAMESPACE, so I just associated them with hex values
    ANYARR_ERR_OOM = 0xF0,
    ANYARR_ERR_NULLPTR = 0xF1,
    ANYARR_ERR_OUT_OF_BOUNDS = 0xF2,
    ANYARR_ERR_EMPTY = 0xF3,
    ANYARR_ERR_TYPE_MISMATCH = 0xF4,
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
    TYPE_PTR, // Not supporting a footgun so the ownership is on your hands not arena and only providing clone and comparison safely with any_print only providing pointer address, but I was thinking of supporting void* to be stored in Arena in the future
    TYPE_ARRAY,
    TYPE_MAP
};


static inline anyarr_result handle_error(const anyarr_result error_code) {
    switch (error_code) {
        case ANYARR_ERR_OOM:
            fprintf(stderr, "[ANYARR] Out of Memory. Exiting...\n");
            abort();
        case ANYARR_ERR_NULLPTR:
            fprintf(stderr, "[ANYARR] Null pointer was passed.\n");
            break;
        case ANYARR_ERR_OUT_OF_BOUNDS:
            fprintf(stderr, "[ANYARR] Index out of bounds.\n");
            break;
        case ANYARR_ERR_EMPTY:
            fprintf(stderr, "[ANYARR] Key or index not found.\n");
            break;
        case ANYARR_ERR_TYPE_MISMATCH:
            fprintf(stderr, "[ANYARR] Type mismatch.\n");
            break;
        default:
            break;
    }
    return error_code;
}

// This is very fast because it's just pointer bumps for alloc/dealloc
typedef struct {
    uint8_t *base;
    size_t used;
    size_t committed;
    size_t reserved;
    uint64_t hash_seed;
    uint64_t hash_seed_c1;
} ARENA_NAMESPACE;

#ifdef ANYARR_IMPLEMENTATION
ARENA_NAMESPACE anyarr_arena_instance;
ARENA_NAMESPACE *anyarr_arena = NULL;
#else
extern ARENA_NAMESPACE anyarr_arena_instance;
extern ARENA_NAMESPACE *anyarr_arena;
#endif


static inline uint64_t make_seed(void) {
    uint64_t seed = 0;
#ifdef ANYARR_PLATFORM_WINDOWS // Spent WAY TOO LONG figuring out how to get random number without BCrypt but here is reference: https://github.com/jedisct1/libsodium/blob/master/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c
#define RtlGenRandom SystemFunction036
    BOOLEAN NTAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
    RtlGenRandom(&seed, sizeof(seed));
#else
    getentropy(&seed, sizeof(seed)); // getrandom() is not required for this use case, it only needs to init once and the seed !>255
#endif
    if (seed == 0) {
        fprintf(stderr, "Seed failed. Aborting...");
        abort();
    }
    return seed;
}


static inline size_t arena_align_up(size_t n) {
    return (n + 15) & ~15;
}


static inline anyarr_result arena_commit(ARENA_NAMESPACE *a, const size_t extra) {
    size_t new_committed = (a->committed + extra + ARENA_COMMIT_CHUNK - 1) & ~(ARENA_COMMIT_CHUNK - 1);
    if (new_committed > a->reserved) {
        new_committed = a->reserved;
    }
    if (new_committed <= a->committed) {
        return handle_error(ANYARR_ERR_OOM);
    }
    size_t delta = new_committed - a->committed;
#ifdef ANYARR_PLATFORM_WINDOWS
    if (VirtualAlloc(a->base + a->committed, delta, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        return handle_error(ANYARR_ERR_OOM);
    }
#else
    if (mprotect(a->base + a->committed, delta, PROT_READ | PROT_WRITE) != 0) {
        return handle_error(ANYARR_ERR_OOM);
    }
#endif
    a->committed = new_committed;
    return ANYARR_OK;
}


static inline void auto_init(void);

static inline anyarr_result arena_alloc(ARENA_NAMESPACE *a, const size_t size, void **out) {
    if (__builtin_expect(anyarr_arena == NULL, 0)) {
        auto_init();
    }
    if (a == NULL) {
        a = anyarr_arena;
    }
    if (a->base == NULL || size == 0 || out == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    const size_t new_used = a->used + arena_align_up(size);
    if (new_used > a->reserved) {
        return handle_error(ANYARR_ERR_OOM);
    }
    if (new_used > a->committed) {
        if (arena_commit(a, new_used - a->committed) != ANYARR_OK) {
            return handle_error(ANYARR_ERR_OOM);
        }
    }
    *out = a->base + a->used;
    a->used = new_used;
    return ANYARR_OK;
}


static inline anyarr_result arena_init(ARENA_NAMESPACE *a) {
    if (a == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    size_t reserve_size = (ANYARR_RESERVE_SIZE + ARENA_COMMIT_CHUNK - 1) & ~(ARENA_COMMIT_CHUNK - 1);
#ifdef ANYARR_PLATFORM_WINDOWS
    void *base = VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_NOACCESS);
    if (base == NULL) {
        return handle_error(ANYARR_ERR_OOM);
    }
#else
    void *base = mmap(NULL, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return handle_error(ANYARR_ERR_OOM);
    }
#endif
    a->base = (uint8_t *) base;
    a->used = 0;
    a->committed = 0;
    a->reserved = reserve_size;
    return ANYARR_OK;
}


static inline anyarr_result arena_reset(ARENA_NAMESPACE *a) {
    if (a == NULL || a->base == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    a->used = 0;
    return ANYARR_OK;
}


static inline anyarr_result arena_free(ARENA_NAMESPACE *a) {
    if (a->base == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
#ifdef ANYARR_PLATFORM_WINDOWS
    VirtualFree(a->base, 0, MEM_RELEASE);
#else
    munmap(a->base, a->reserved);
#endif
    a->base = NULL;
    a->used = 0;
    a->committed = 0;
    a->reserved = 0;
    return ANYARR_OK;
}


static inline size_t arena_save(const ARENA_NAMESPACE *a) {
    const ARENA_NAMESPACE *arena = a;
    if (arena == NULL) {
        arena = anyarr_arena;
    }
    return arena->used;
}

static inline void arena_restore(ARENA_NAMESPACE *a, const size_t saved) {
    if (a == NULL) {
        a = anyarr_arena;
    }
    if (saved >= a->used) {
        return;
    }
    a->used = saved;
}


static inline void checkpoint_cleanup(const size_t *cp) {
    arena_restore(anyarr_arena, *cp);
}

#define ARENA_TEMP __attribute__((cleanup(checkpoint_cleanup))) size_t


static inline void auto_cleanup(void) {
    if (anyarr_arena != NULL) {
        arena_free(anyarr_arena);
        anyarr_arena = NULL;
    }
}

static inline void auto_init(void) {
    if (anyarr_arena != NULL) {
        return;
    }
    static const uint64_t WY1 = 0xe7037ed1a0b428dbull;
    arena_init(&anyarr_arena_instance);
    anyarr_arena = &anyarr_arena_instance;
    anyarr_arena->hash_seed = make_seed();
    anyarr_arena->hash_seed_c1 = anyarr_arena->hash_seed ^ WY1;
    atexit(auto_cleanup);
}


static inline void arena_cleanup(ARENA_NAMESPACE *ap) {
    if (ap) {
        arena_free(ap);
    }
}

#define ARENA_SCOPED __attribute__((cleanup(arena_cleanup))) ARENA_NAMESPACE = {0}


typedef struct DynamicArray_ DynamicArray_;
typedef struct HashMap_ HashMap_;
typedef struct Blob Blob;

typedef struct {
    union {
        struct {
            uint8_t type;

            union {
                _Bool b;
                char c;
                int64_t i;
                uint64_t u;
                float f;
                double d;
                char *s;
                Blob *l;
                void *p;
                DynamicArray_ *a;
                HashMap_ *m;
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

struct DynamicArray_ {
    ANY_NAMESPACE *data;
    size_t size;
    size_t capacity;
};

#define CTRL_EMPTY 0xFF
#define CTRL_DELETED 0xFE
struct HashMap_ {
    // By default, the fingerprint stores 0xFF, but it gets override by an actual fingerprint and when it gets deleted it again gets override to 0xFE making it act also as control byte
    uint8_t *fingerprint;
    char **key;
    ANY_NAMESPACE *value;
    size_t size;
    size_t capacity;
    size_t tombstone;   // Keeps track of how many slots are deleted so when we trigger a resize we already know how many entries are deleted instead of empty
};

struct Blob {
    uint8_t *ptr;
    size_t size;
};

typedef struct {
    uint8_t type;
    size_t index;
    size_t bound;
    ANY_NAMESPACE *data;
    uint8_t *fingerprint;
    char **key;
    ANY_NAMESPACE *value;
    const char *last_key;
} AnyIter;


static inline ANY_NAMESPACE assign_null(void) {
    return (ANY_NAMESPACE){TYPE_NULL};
}


static inline ANY_NAMESPACE assign_bool(const _Bool b) {
    return (ANY_NAMESPACE){TYPE_BOOL, .data.b = b};
}


static inline ANY_NAMESPACE assign_char(const char c) {
    return (ANY_NAMESPACE){TYPE_CHAR, .data.c = c};
}


static inline ANY_NAMESPACE assign_int_(const int64_t i) {  // Internal until number arrays are made
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


static inline ANY_NAMESPACE assign_string(const char *s) {
    if (s == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return (ANY_NAMESPACE){ANYARR_ERR_NULLPTR};
    }
    const size_t len = strlen(s);
    if (len < 15) {
        ANY_NAMESPACE val = {._type_sso = TYPE_STRING_SMALL};
        memcpy(val.small_buf, s, len + 1);
        return val;
    }
    char *dup;
    arena_alloc(anyarr_arena, len + 1, (void **) &dup);
    memcpy(dup, s, len + 1);
    return (ANY_NAMESPACE){.type = TYPE_STRING, .data.s = dup};
}


static inline ANY_NAMESPACE assign_blob(const Blob *l) {
    if (l == NULL || l->ptr == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return (ANY_NAMESPACE){ANYARR_ERR_NULLPTR};
    }
    if (l->size < 15) {
        ANY_NAMESPACE val = {._type_sbo = TYPE_BLOB_SMALL, .len = l->size};
        memcpy(val.small_blob, l->ptr, l->size);
        return val;
    }
    Blob *dup;
    arena_alloc(anyarr_arena, sizeof(Blob), (void **) &dup);
    arena_alloc(anyarr_arena, l->size, (void **) &dup->ptr);
    memcpy(dup->ptr, l->ptr, l->size);
    dup->size = l->size;
    return (ANY_NAMESPACE){.type = TYPE_BLOB, .data.l = dup};
}


static inline ANY_NAMESPACE assign_ptr(void *p) {
    if (p == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return (ANY_NAMESPACE){ANYARR_ERR_NULLPTR};
    }
    return (ANY_NAMESPACE){TYPE_PTR, .data.p = p};
}


static inline anyarr_result array_init(DynamicArray_ *buf);

static inline ANY_NAMESPACE assign_array(DynamicArray_ *a) {
    if (a == NULL) {
        DynamicArray_ *heap_arr;
        arena_alloc(anyarr_arena, sizeof(DynamicArray_), (void **) &heap_arr);
        array_init(heap_arr);
        return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = heap_arr};
    }
    return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = a};
}


static inline anyarr_result map_init(HashMap_ *m);

static inline ANY_NAMESPACE assign_map(HashMap_ *m) {
    if (m == NULL) {
        HashMap_ *heap_map;
        arena_alloc(anyarr_arena, sizeof(HashMap_), (void **) &heap_map);
        map_init(heap_map);
        return (ANY_NAMESPACE){TYPE_MAP, .data.m = heap_map};
    }
    return (ANY_NAMESPACE){TYPE_MAP, .data.m = m};
}


static inline anyarr_result any_get_bool(const ANY_NAMESPACE *val, _Bool *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_BOOL) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.b;
    return ANYARR_OK;
}


static inline anyarr_result any_get_char(const ANY_NAMESPACE *val, char *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_CHAR) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.c;
    return ANYARR_OK;
}


static inline anyarr_result any_get_int(const ANY_NAMESPACE *val, int64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_INT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.i;
    return ANYARR_OK;
}


static inline anyarr_result any_get_uint(const ANY_NAMESPACE *val, uint64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_UINT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.u;
    return ANYARR_OK;
}


static inline anyarr_result any_get_float(const ANY_NAMESPACE *val, float *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_FLOAT) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.f;
    return ANYARR_OK;
}


static inline anyarr_result any_get_double(const ANY_NAMESPACE *val, double *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_DOUBLE) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.d;
    return ANYARR_OK;
}


static inline anyarr_result any_get_string(const ANY_NAMESPACE *val, const char **out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (val->type == TYPE_STRING) {
        *out_value = val->data.s;
        return ANYARR_OK;
    } else if (val->type == TYPE_STRING_SMALL) {
        *out_value = val->small_buf;
        return ANYARR_OK;
    }
    return handle_error(ANYARR_ERR_TYPE_MISMATCH);
}


static inline anyarr_result any_get_blob(const ANY_NAMESPACE *val, Blob *out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
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
    return handle_error(ANYARR_ERR_TYPE_MISMATCH);
}


static inline anyarr_result any_get_ptr(const ANY_NAMESPACE *val, void **out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_PTR) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.p;
    return ANYARR_OK;
}


static inline anyarr_result any_get_array(const ANY_NAMESPACE *val, DynamicArray_ **out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_ARRAY) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.a;
    return ANYARR_OK;
}


static inline anyarr_result any_get_map(const ANY_NAMESPACE *val, HashMap_ **out_value) {
    if (val == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    } else if (val->type != TYPE_MAP) {
        return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
    *out_value = val->data.m;
    return ANYARR_OK;
}


static inline AnyIter any_iter(const ANY_NAMESPACE *root);
static inline ANY_NAMESPACE *any_iter_next(AnyIter *it);

static inline anyarr_result any_print_impl(ANY_NAMESPACE *val, const int depth) {
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
            printf("int64_t: %lld\n", val->data.i);
            return ANYARR_OK;

        case TYPE_UINT:
            INDENT();
            printf("uint64_t: %llu\n", val->data.u);
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
                any_print_impl(item, depth + 1);
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
                printf("%s:\n", it.last_key);
                any_print_impl(item, depth + 2);
            }
            INDENT();
            printf("}\n");
            return ANYARR_OK;
        }

        #undef INDENT
        default:
            return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
}
static inline anyarr_result any_print_helper(ANY_NAMESPACE val) {
    return any_print_impl(&val, 0);
}
#define any_print(x) _Generic((x),              \
    ANY_NAMESPACE*: any_print_impl((x), 0),     \
    default: any_print_helper(assign_any(x))    \
)


static inline uint64_t map_hash(const char *key) {
    static const uint64_t WY0 = 0xa0761d6478bd642full;
    static const uint64_t WY1 = 0xe7037ed1a0b428dbull;
    size_t len = strlen(key);
    const uint8_t *p = (const uint8_t *)key;
    uint64_t seed = anyarr_arena->hash_seed;
#ifdef __AVX512DQ__ // I am NOT writing anything besides AVX512 other's have too much boilerplate for me to care
    __m512i seeds = _mm512_set1_epi64(anyarr_arena->hash_seed_c1);
    __m512i c0 = _mm512_set1_epi64(WY0);
    for (; len >= 64; len -= 64, p += 64) {
        __m512i chunk = _mm512_loadu_si512(p);
        __m512i a = _mm512_xor_si512(chunk, c0);
        __m512i mul_64 = _mm512_mullo_epi64(a, seeds);
        seeds = _mm512_xor_si512(seeds, mul_64);
    }
    if (len > 0) {
        __mmask64 tail_mask = (1ULL << len) - 1;
        __m512i tail = _mm512_maskz_loadu_epi8(tail_mask, p);
        __m512i a = _mm512_xor_si512(tail, c0);
        __m512i mul_64 = _mm512_mullo_epi64(a, seeds);
        seeds = _mm512_xor_si512(seeds, mul_64);
    }
    seed = _mm512_reduce_xor_epi64(seeds);
#else
    for (; len >= 8; len -= 8, p += 8) {
        uint64_t a = 0, b = 0;
        memcpy(&a, p, 4);
        memcpy(&b, p + 4, 4);
        const __uint128_t m = (__uint128_t)(a ^ WY0) * (b ^ WY1);
        seed ^= (uint64_t)(m) ^ (uint64_t)(m >> 64);
    }
    uint64_t a = 0, b = 0;
    switch (len) {
        case 7:
            b  = (uint64_t)p[6] << 32;
        case 6:
            b |= (uint64_t)p[5] << 16;
        case 5:
            b |= (uint64_t)p[4] <<  8;
        case 4:
            memcpy(&a, p, 4);
            break;
        case 3:
            a  = (uint64_t)p[2] << 16;
        case 2:
            a |= (uint64_t)p[1] <<  8;
        case 1:
            a |= (uint64_t)p[0]; b = 0;
            break;
        case 0:
            a = 0; b = 0;
            break;
        default:
            fprintf(stderr, "[ANYARR] Unexpected case in map hashing."); // Just there to satisfy the ide complaining about missing default case
            break;
    }
    const __uint128_t m = (__uint128_t)(a ^ WY0) * (b ^ WY1);
    seed ^= (uint64_t)(m) ^ (uint64_t)(m >> 64);
#endif
    const __uint128_t f = (__uint128_t)(seed ^ WY0) * (seed ^ WY1);
    return (uint64_t)(f) ^ (uint64_t)(f >> 64);
}


static inline anyarr_result array_init(DynamicArray_ *buf) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    buf->size = 0;
    buf->capacity = 4;
    arena_alloc(anyarr_arena, buf->capacity * sizeof(ANY_NAMESPACE), (void **) &buf->data);
    return ANYARR_OK;
}


static inline anyarr_result map_init(HashMap_ *m) {
    if (m == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    m->size = 0;
    m->capacity = 16;
    m->tombstone = 0;
    arena_alloc(anyarr_arena, m->capacity * sizeof(uint8_t), (void **) &m->fingerprint);
    arena_alloc(anyarr_arena, m->capacity * sizeof(char*), (void **) &m->key);
    arena_alloc(anyarr_arena, m->capacity * sizeof(ANY_NAMESPACE), (void **) &m->value);
    memset(m->fingerprint, CTRL_EMPTY, m->capacity);
    return ANYARR_OK;
}


static inline anyarr_result map_resize(HashMap_ *m);


static inline anyarr_result map_get(const HashMap_ *m, const char *key, ANY_NAMESPACE **out_value) {
    if (m == NULL || m->key == NULL || key == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    const uint64_t hash = map_hash(key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;  // There's a 1/255 chance that the fingerprint itself can store 0xFF or 0xFE as value so we truncate the first bit so it never reaches that range
    size_t index = hash & (m->capacity - 1);
    while (1) {
        const uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            return handle_error(ANYARR_ERR_EMPTY);
        }
        if (ctrl == fingerprint && strcmp(m->key[index], key) == 0) {
            *out_value = &m->value[index];
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
}


static inline anyarr_result map_get_silent(const HashMap_ *m, const char *key, ANY_NAMESPACE **out_value) {
    if (m == NULL || m->key == NULL || key == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    const uint64_t hash = map_hash(key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;
    size_t index = hash & (m->capacity - 1);
    while (1) {
        const uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            return ANYARR_ERR_EMPTY;
        }
        if (ctrl == fingerprint && strcmp(m->key[index], key) == 0) {
            *out_value = &m->value[index];
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
}


static inline anyarr_result map_put_impl(HashMap_ *m, const char *key, const ANY_NAMESPACE value) {
    if (m == NULL || m->key == NULL || key == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    ANY_NAMESPACE *existing;
    if (map_get_silent(m, key, &existing) == ANYARR_OK) {
        *existing = value;
        return ANYARR_OK;
    }
    if ((m->size + m->tombstone + 1) * 4 >= m->capacity * 3) {
        map_resize(m);
    }
    const uint64_t hash = map_hash(key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;
    const size_t key_len = strlen(key);
    char *current_key;
    arena_alloc(anyarr_arena, key_len + 1, (void **) &current_key);
    memcpy(current_key, key, key_len + 1);
    size_t index = hash & (m->capacity - 1);
    while (1) {
        uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
            if (ctrl == CTRL_DELETED) m->tombstone--;
            m->key[index] = current_key;
            m->value[index] = value;
            m->fingerprint[index] = fingerprint;
            m->size++;
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
}
#define map_put(m, key, value) map_put_impl(m, key, assign_any(value))


static inline anyarr_result map_resize(HashMap_ *m) {
    const uint8_t *old_fingerprint = m->fingerprint;
    char **old_key = m->key;
    const ANY_NAMESPACE *old_value  = m->value;
    const size_t old_capacity = m->capacity;
    size_t new_capacity = old_capacity;
    if (m->size * 2 >= old_capacity) {
        new_capacity = old_capacity << 1;
    }
    uint8_t *new_fingerprint = NULL;
    char **new_key = NULL;
    ANY_NAMESPACE *new_value = NULL;
    arena_alloc(anyarr_arena, new_capacity * sizeof(uint8_t), (void **) &new_fingerprint);
    arena_alloc(anyarr_arena, new_capacity * sizeof(char *), (void **) &new_key);
    arena_alloc(anyarr_arena, new_capacity * sizeof(ANY_NAMESPACE), (void **) &new_value);
    memset(new_fingerprint, CTRL_EMPTY, new_capacity);
    m->fingerprint = new_fingerprint;
    m->key = new_key;
    m->value = new_value;
    m->capacity = new_capacity;
    m->size = 0;
    m->tombstone = 0;
    for (size_t i = 0; i < old_capacity; i++) {
        const uint8_t ctrl = old_fingerprint[i];
        if (ctrl < 0x80) {
            size_t index = map_hash(old_key[i]) & (m->capacity - 1);
            while (m->fingerprint[index] != CTRL_EMPTY) {
                index = (index + 1) & (m->capacity - 1);
            }
            m->key[index] = old_key[i];
            m->value[index] = old_value[i];
            m->fingerprint[index] = ctrl;
            m->size++;
        }
    }
    return ANYARR_OK;
}


static inline anyarr_result map_remove(HashMap_ *m, const char *key) {
    if (m == NULL || m->key == NULL || key == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    const uint64_t hash = map_hash(key);
    const uint8_t fingerprint = (uint8_t)(hash >> 56) & 0x7F;
    size_t index = hash & (m->capacity - 1);
    while (1) {
        uint8_t ctrl = m->fingerprint[index];
        if (ctrl == CTRL_EMPTY) {
            return handle_error(ANYARR_ERR_EMPTY);
        }
        if (ctrl == fingerprint && strcmp(m->key[index], key) == 0) {
            m->fingerprint[index] = CTRL_DELETED;
            m->size--;
            m->tombstone++;
            return ANYARR_OK;
        }
        index = (index + 1) & (m->capacity - 1);
    }
}


static inline anyarr_result array_append_impl(DynamicArray_ *buf, const ANY_NAMESPACE value) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (buf->size == buf->capacity) {
        size_t new_capacity = 4;
        if (buf->capacity != 0) {
            new_capacity = buf->capacity + (buf->capacity >> 1);
        }
        const size_t old_bytes = arena_align_up(buf->capacity * sizeof(ANY_NAMESPACE));
        const size_t new_bytes = arena_align_up(new_capacity * sizeof(ANY_NAMESPACE));
        const _Bool at_tip = (buf->data != NULL) && ((uint8_t *) buf->data + old_bytes == anyarr_arena->base + anyarr_arena->used);
        if (at_tip) {
            const size_t extra = new_bytes - old_bytes;
            const size_t new_used = anyarr_arena->used + extra;
            if (new_used > anyarr_arena->reserved) {
                return handle_error(ANYARR_ERR_OOM);
            }
            if (new_used > anyarr_arena->committed) {
                arena_commit(anyarr_arena, new_used - anyarr_arena->committed);
            }
            anyarr_arena->used = new_used;
            buf->capacity = new_capacity;
        } else {
            ANY_NAMESPACE *temp;
            arena_alloc(anyarr_arena, new_capacity * sizeof(ANY_NAMESPACE), (void **) &temp);
            if (buf->size > 0) {
                memcpy(temp, buf->data, buf->size * sizeof(ANY_NAMESPACE));
            }
            buf->data = temp;
            buf->capacity = new_capacity;
        }
    }
    buf->data[buf->size++] = value;
    return ANYARR_OK;
}
#define array_append(buf, value) array_append_impl(buf, assign_any(value))


static inline anyarr_result array_remove_index(DynamicArray_ *buf, const size_t index) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
    }
    const size_t index_queue = buf->size - index - 1;
    if (index_queue > 0) {
        memmove(&buf->data[index], &buf->data[index + 1], index_queue * sizeof(ANY_NAMESPACE));
    }
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result array_set_index_impl(const DynamicArray_ *buf, const size_t index, const ANY_NAMESPACE value) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
    }
    buf->data[index] = value;
    return ANYARR_OK;
}
#define array_set_index(buf, index, value) array_set_index_impl(buf, index, assign_any(value))


static inline anyarr_result array_get(const DynamicArray_ *buf, const size_t index, ANY_NAMESPACE **out_value) {
    if (buf == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (index >= buf->size) {
        return handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
    }

    *out_value = &buf->data[index];
    return ANYARR_OK;
}


static inline anyarr_result any_get_path(ANY_NAMESPACE *root, const char *path, ANY_NAMESPACE **out_value) {
    if (root == NULL || path == NULL || out_value == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    ANY_NAMESPACE *current = root;
    const char *p = path;
    char segment[256];  // Not worth making it arena allocated
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
                return handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
            }
            segment[i++] = *p++;
        }
        segment[i] = '\0';
        if (current->type == TYPE_MAP) {
            const anyarr_result r = map_get_silent(current->data.m, segment, &current);
            if (r != ANYARR_OK) {
                return handle_error(r);
            }
        } else if (current->type == TYPE_ARRAY) {
            char *end_ptr;
            const size_t index = strtoull(segment, &end_ptr, 10);
            if (*end_ptr != '\0') {
                return handle_error(ANYARR_ERR_TYPE_MISMATCH);
            }
            const anyarr_result r = array_get(current->data.a, index, &current);
            if (r != ANYARR_OK) {
                return r;
            }
        } else {
            return handle_error(ANYARR_ERR_TYPE_MISMATCH);
        }
    }
    *out_value = current;
    return ANYARR_OK;
}


static inline anyarr_result array_reserve(DynamicArray_ *buf, const size_t new_capacity) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (new_capacity <= buf->capacity) {
        return ANYARR_OK;
    }
    ANY_NAMESPACE *temp;
    arena_alloc(anyarr_arena, new_capacity * sizeof(ANY_NAMESPACE), (void **) &temp);
    if (buf->size > 0 && buf->data != NULL) {
        memcpy(temp, buf->data, buf->size * sizeof(ANY_NAMESPACE));
    }
    buf->data = temp;
    buf->capacity = new_capacity;
    return ANYARR_OK;
}


static inline anyarr_result any_clone(const ANY_NAMESPACE *src, ANY_NAMESPACE *dest) {
    if (src == NULL || dest == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
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
            *dest = assign_string(src->data.s);
            return ANYARR_OK;
        case TYPE_ARRAY: {
            DynamicArray_ *src_arr = src->data.a;
            DynamicArray_ *new_arr;
            arena_alloc(anyarr_arena, sizeof(DynamicArray_), (void **) &new_arr);
            array_init(new_arr);
            for (size_t i = 0; i < src_arr->size; i++) {
                ANY_NAMESPACE cloned_elem;
                const anyarr_result res = any_clone(&src_arr->data[i], &cloned_elem);
                if (res != ANYARR_OK) {
                    return res;
                }
                array_append_impl(new_arr, cloned_elem);
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
            *dest = assign_blob(&b);
            return ANYARR_OK;
        }
        case TYPE_MAP: {
            const HashMap_ *src_map = src->data.m;
            HashMap_ *new_map;
            arena_alloc(anyarr_arena, sizeof(HashMap_), (void **) &new_map);
            map_init(new_map);
            for (size_t i = 0; i < src_map->capacity; i++) {
                const uint8_t ctrl = src_map->fingerprint[i];
                if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
                    continue;
                }
                ANY_NAMESPACE cloned_val;
                const anyarr_result res = any_clone(&src_map->value[i], &cloned_val);
                if (res != ANYARR_OK) {
                    return res;
                }
                map_put_impl(new_map, src_map->key[i], cloned_val);
            }
            *dest = assign_map(new_map);
            return ANYARR_OK;
        }
        default:
            break;
    }
    return handle_error(ANYARR_ERR_TYPE_MISMATCH);
}


static inline anyarr_result any_equal(const ANY_NAMESPACE *a, const ANY_NAMESPACE *b) {
    if (a == NULL || b == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
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

    if ((a->type == TYPE_BLOB || a->type == TYPE_BLOB_SMALL) && (b->type == TYPE_BLOB || b->type == TYPE_BLOB_SMALL)) {
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
            const DynamicArray_ *aa = a->data.a;
            const DynamicArray_ *ab = b->data.a;
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
            const HashMap_ *ma = a->data.m;
            const HashMap_ *mb = b->data.m;
            if (ma->size != mb->size) {
                return ANYARR_NOT_EQUAL;
            }
            for (size_t i = 0; i < ma->capacity; i++) {
                const uint8_t ctrl = ma->fingerprint[i];
                if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) { continue; }
                ANY_NAMESPACE *val_b;
                if (map_get_silent(mb, ma->key[i], &val_b) != ANYARR_OK) {
                    return ANYARR_NOT_EQUAL;
                }
                const anyarr_result res = any_equal(&ma->value[i], val_b);
                if (res != ANYARR_EQUAL) {
                    return res;
                }
            }
            return ANYARR_EQUAL;
        }
        default:
            return handle_error(ANYARR_ERR_TYPE_MISMATCH);
    }
}


static inline anyarr_result array_pop(DynamicArray_ *buf) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    if (buf->size == 0) {
        return handle_error(ANYARR_ERR_EMPTY);
    }
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result array_clear(DynamicArray_ *buf) {
    if (buf == NULL) {
        return handle_error(ANYARR_ERR_NULLPTR);
    }
    buf->size = 0;
    return ANYARR_OK;
}


static inline const ANY_NAMESPACE *array_at(const DynamicArray_ *buf, size_t idx) {
    if (buf == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return NULL;
    }
    if (idx >= buf->size) {
        handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
        return NULL;
    }
    return &buf->data[idx];
}


static inline AnyIter any_iter(const ANY_NAMESPACE *root) {
    AnyIter it = {0};
    if (root == NULL) {
        return it;
    }
    if (root->type == TYPE_ARRAY) {
        it.type = TYPE_ARRAY;
        it.data = root->data.a->data;
        it.bound = root->data.a->size;
    } else if (root->type == TYPE_MAP) {
        it.type = TYPE_MAP;
        it.fingerprint = root->data.m->fingerprint;
        it.key = root->data.m->key;
        it.value = root->data.m->value;
        it.bound = root->data.m->capacity;
    }
    return it;
}


static inline ANY_NAMESPACE *any_iter_next(AnyIter *it) {
    if (it->type == TYPE_ARRAY) {
        if (it->index < it->bound) {
            return &it->data[it->index++];
        }
        return NULL;
    }

    if (it->type == TYPE_MAP) {
        while (it->index < it->bound) {
            uint8_t ctrl = it->fingerprint[it->index];
            if (ctrl >= 0x80) {
                it->index++;
                continue;
            }
            it->last_key = it->key[it->index];
            return &it->value[it->index++];
        }
        return NULL;
    }
    return NULL;
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
    if (root == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return walk;
    }
    if (root->type != TYPE_ARRAY && root->type != TYPE_MAP) {
        handle_error(ANYARR_ERR_TYPE_MISMATCH);
        return walk;
    }
    walk.stack[0] = any_iter(root);
    walk.depth = 0;
    walk.result = ANYARR_OK;
    return walk;
}


static inline const char *any_walker_key(const AnyWalker *walk) {
    if (walk == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return NULL;
    }
    if (walk->result != ANYARR_OK) {
        handle_error(walk->result);
        return NULL;
    }
    if (walk->depth < 0) {
        return NULL;
    }
    return walk->stack[walk->depth].last_key;
}


static inline ANY_NAMESPACE *any_walk_next(AnyWalker *walk) {
    if (walk == NULL) {
        handle_error(ANYARR_ERR_NULLPTR);
        return NULL;
    }
    if (walk->result != ANYARR_OK) {
        handle_error(walk->result);
        return NULL;
    }
    if (walk->depth < 0) {
        return NULL;
    }
    while (walk->depth >= 0) {
        ANY_NAMESPACE *val = any_iter_next(&walk->stack[walk->depth]);
        if (val == NULL) {
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
            handle_error(ANYARR_ERR_OUT_OF_BOUNDS);
            return NULL;
        }
        walk->stack[next_depth] = any_iter(val);
        walk->depth = next_depth;
    }
    return NULL;
}


#define assign_any(x) _Generic((x), \
    _Bool: assign_bool,             \
    char: assign_char,              \
    signed char: assign_int_,       \
    short: assign_int_,             \
    int: assign_int_,               \
    long: assign_int_,              \
    long long: assign_int_,         \
    unsigned char: assign_uint,     \
    unsigned short: assign_uint,    \
    unsigned int: assign_uint,      \
    unsigned long: assign_uint,     \
    unsigned long long: assign_uint,\
    float: assign_float,            \
    double: assign_double,          \
    char*: assign_string,           \
    const char*: assign_string,     \
    Blob*: assign_blob,             \
    DynamicArray_*: assign_array,   \
    HashMap_*: assign_map,          \
    default: assign_ptr             \
)(x)

#define get_any(val_ptr, out_ptr) _Generic((out_ptr),   \
    _Bool*: any_get_bool,                               \
    char*: any_get_char,                                \
    int64_t*: any_get_int,                              \
    uint64_t*: any_get_uint,                            \
    float*: any_get_float,                              \
    double*: any_get_double,                            \
    const char**: any_get_string,                       \
    Blob*: any_get_blob,                                \
    DynamicArray_**: any_get_array,                     \
    HashMap_**: any_get_map,                            \
    void**: any_get_ptr                                 \
)(val_ptr, out_ptr)

// This is where it gets messy, it's a hacky macro which takes in the struct and uses the _Generic to assign DynamicArray and HashMap structure and iterates any_get_path
#define path_get(root, path, out_ptr)                                      \
get_any(({                                                                 \
    ANY_NAMESPACE *_v = NULL;                                              \
    ANY_NAMESPACE _tmp;                                                    \
    ANY_NAMESPACE *_root = _Generic((root),                                \
    ANY_NAMESPACE*: (root),                                                \
    DynamicArray_*: (_tmp = assign_array((DynamicArray_*)(root)), &_tmp),  \
    HashMap_*: (_tmp = assign_map((HashMap_*)(root)), &_tmp)               \
);                                                                         \
any_get_path(_root, (path), &_v);                                          \
_v;                                                                        \
}), (out_ptr))

// Internal macro to handle iteration of HashMap and DynamicArray
#define _any_iter_generic(root_ptr) _Generic((root_ptr),                                            \
    ANY_NAMESPACE*: any_iter((ANY_NAMESPACE*)(root_ptr)),                                           \
    DynamicArray_*: any_iter(&(ANY_NAMESPACE){TYPE_ARRAY, .data.a = (DynamicArray_*)(root_ptr)}),   \
    HashMap_*: any_iter(&(ANY_NAMESPACE){TYPE_MAP,   .data.m = (HashMap_*)(root_ptr)})              \
)

#define foreach(item, root_ptr)                                          \
    for (AnyIter _it = _any_iter_generic(root_ptr);                      \
    (item = any_iter_next(&_it)) != NULL; )

#define foreach_kv(key, item, root_ptr)                                  \
    for (AnyIter _it = _any_iter_generic(root_ptr);                      \
    (item = any_iter_next(&_it)) ? (key = _it.last_key, 1) : 0; )

#define HashMap(name)  \
    HashMap_ name;     \
    map_init(&name)

#define DynamicArray(name)   \
    DynamicArray_ name;      \
    array_init(&name)

#endif
/*
 * This library should work with C11 onwards flawlessly but C99 needs a lot of setup to work and generally not recommended
 * To make it work on C99, you might need to use the GNU C99 standard instead of strict C99 for anonymous structs to work
 * There is also no support for generic macros so remove the macros at the bottom of the file and stick with specific datatypes
 * Another note, cleanup attribute is only a part of GCC/Clang not a part of C standard so if you are not using those compilers you will have to manually free memory
 * Also this library is made to be as fast as possible while being easy to use so it's not exactly efficient on memory usage
*/

#ifndef ANYARR_H
#define ANYARR_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*TO DO:
 * Making example code and writing docs
 * Maybe looking at SIMD optimizations after arena
 * Virtual Arena allocator so need an os check for mmap (unix) and VirtualAlloc (Windows)
 * any_iteration()? for looping through the arrays
 * get_map() and get_array() for nested traversal
 * Checks for gcc/clang and enforce RAII semantics through compiler and if the compiler doesn't support it then throw a warning inside the ide to manually cleanup memory
 * Synthetic benchmarks compared to other libraries in AWS
 */


#ifndef ANY_NAMESPACE // just a QOL feature, they can rename "Any" to prevent namespace pollution
#define ANY_NAMESPACE Any
#endif

typedef enum {
    ANYARR_OK,
    ANYARR_EQUAL,
    ANYARR_NOT_EQUAL,
    ANYARR_ERR_OOM,
    ANYARR_ERR_NULLPTR,
    ANYARR_ERR_OUT_OF_BOUNDS,
    ANYARR_ERR_EMPTY,
    ANYARR_ERR_TYPE_MISMATCH
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
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_MAP
};

typedef struct DynamicArray DynamicArray;
typedef struct HashMap HashMap;
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

typedef enum {
    MAP_SLOT_EMPTY,
    MAP_SLOT_OCCUPIED,
    MAP_SLOT_DELETED
} MapSlotState;

typedef struct {
    char *key;
    ANY_NAMESPACE value;
    MapSlotState state;
} MapEntry;

struct HashMap {
    MapEntry *entries;
    size_t deleted_count;
    size_t size;
    size_t capacity;
};

struct Blob {
    uint8_t *ptr;
    size_t size;
};


static inline ANY_NAMESPACE any_make_null() {
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


static inline ANY_NAMESPACE assign_string(const char *s) {
    if (s == NULL) {
        return (ANY_NAMESPACE){TYPE_NULL};
    }
    const size_t len = strlen(s);
    if (len < 15) {
        ANY_NAMESPACE val = {._type_sso = TYPE_STRING_SMALL};
        memcpy(val.small_buf, s, len + 1);
        return val;
    }
    char *dup = malloc(len + 1);
    if (dup == NULL) {
        return any_make_null();
    }
    memcpy(dup, s, len + 1);
    return (ANY_NAMESPACE){.type = TYPE_STRING, .data.s = dup};
}


static inline ANY_NAMESPACE assign_blob(const Blob *l) {
    if (l == NULL || l->ptr == NULL) {
        return (ANY_NAMESPACE){TYPE_NULL};
    }
    if (l->size < 15) {
        ANY_NAMESPACE val = {._type_sbo = TYPE_BLOB_SMALL, .len = l->size};
        memcpy(val.small_blob, l->ptr, l->size);
        return val;
    }
    Blob *dup = malloc(sizeof(Blob));
    if (dup == NULL) {
        return any_make_null();
    }
    dup->ptr = malloc(l->size);
    if (dup->ptr == NULL) {
        free(dup);
        return any_make_null();
    }
    memcpy(dup->ptr, l->ptr, l->size);
    dup->size = l->size;
    return (ANY_NAMESPACE){.type = TYPE_BLOB, .data.l = dup};
}


static inline ANY_NAMESPACE assign_ptr(void *p) {
    if (p == NULL) {
        return any_make_null();
    }
    return (ANY_NAMESPACE){TYPE_PTR, .data.p = p};
}


static inline anyarr_result any_init(DynamicArray *buf);

static inline ANY_NAMESPACE assign_array(DynamicArray *a) {
    if (a == NULL) {
        DynamicArray *heap_arr = malloc(sizeof(DynamicArray));
        if (heap_arr == NULL || any_init(heap_arr) != ANYARR_OK) {
            free(heap_arr);
            return any_make_null();
        }
        return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = heap_arr};
    }
    return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = a};
}


static inline anyarr_result map_init(HashMap *m);

static inline ANY_NAMESPACE assign_map(HashMap *m) {
    if (m == NULL) {
        HashMap *heap_map = malloc(sizeof(HashMap));
        if (heap_map == NULL || map_init(heap_map) != ANYARR_OK) {
            free(heap_map);
            return any_make_null();
        }
        return (ANY_NAMESPACE){TYPE_MAP, .data.m = heap_map};
    }
    return (ANY_NAMESPACE){TYPE_MAP, .data.m = m};
}


static inline _Bool any_is_null(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_NULL;
}

static inline _Bool any_is_bool(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_BOOL;
}

static inline _Bool any_is_char(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_CHAR;
}

static inline _Bool any_is_int(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_INT;
}

static inline _Bool any_is_uint(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_UINT;
}

static inline _Bool any_is_float(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_FLOAT;
}

static inline _Bool any_is_double(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_DOUBLE;
}

static inline _Bool any_is_string(const ANY_NAMESPACE *val) {
    return val && (val->type == TYPE_STRING || val->type == TYPE_STRING_SMALL);
}

static inline _Bool any_is_blob(const ANY_NAMESPACE *val) {
    return val && (val->type == TYPE_BLOB || val->type == TYPE_BLOB_SMALL);
}

static inline _Bool any_is_array(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_ARRAY;
}

static inline _Bool any_is_map(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_MAP;
}

static inline _Bool any_is_ptr(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_PTR;
}


static inline anyarr_result any_get_bool(const ANY_NAMESPACE *val, _Bool *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_BOOL) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.b;
    return ANYARR_OK;
}


static inline anyarr_result any_get_char(const ANY_NAMESPACE *val, char *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_CHAR) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.c;
    return ANYARR_OK;
}


static inline anyarr_result any_get_int(const ANY_NAMESPACE *val, int64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_INT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.i;
    return ANYARR_OK;
}


static inline anyarr_result any_get_uint(const ANY_NAMESPACE *val, uint64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_UINT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.u;
    return ANYARR_OK;
}


static inline anyarr_result any_get_float(const ANY_NAMESPACE *val, float *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_FLOAT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.f;
    return ANYARR_OK;
}


static inline anyarr_result any_get_double(const ANY_NAMESPACE *val, double *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_DOUBLE) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.d;
    return ANYARR_OK;
}


static inline anyarr_result any_get_string(const ANY_NAMESPACE *val, const char **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type == TYPE_STRING) {
        *out_value = val->data.s;
        return ANYARR_OK;
    } else if (val->type == TYPE_STRING_SMALL) {
        *out_value = val->small_buf;
        return ANYARR_OK;
    }
    return ANYARR_ERR_TYPE_MISMATCH;
}


static inline anyarr_result any_get_blob(ANY_NAMESPACE *val, Blob *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type == TYPE_BLOB) {
        out_value->ptr = val->data.l->ptr;
        out_value->size = val->data.l->size;
        return ANYARR_OK;
    } else if (val->_type_sbo == TYPE_BLOB_SMALL) {
        out_value->ptr = (uint8_t *) val->small_blob;
        out_value->size = val->len;
        return ANYARR_OK;
    }
    return ANYARR_ERR_TYPE_MISMATCH;
}


static inline anyarr_result any_get_ptr(const ANY_NAMESPACE *val, void **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_PTR) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.p;
    return ANYARR_OK;
}


static inline anyarr_result any_get_arr(const ANY_NAMESPACE *val, DynamicArray **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_ARRAY) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.a;
    return ANYARR_OK;
}


static inline anyarr_result any_get_map(const ANY_NAMESPACE *val, HashMap **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    } else if (val->type != TYPE_MAP) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.m;
    return ANYARR_OK;
}


static inline _Bool any_as_bool_or(const ANY_NAMESPACE *val, _Bool fallback) {
    if (any_is_bool(val)) {
        return val->data.b;
    }
    return fallback;
}


static inline char any_as_char_or(const ANY_NAMESPACE *val, char fallback) {
    if (any_is_char(val)) {
        return val->data.c;
    }
    return fallback;
}


static inline int64_t any_as_int_or(const ANY_NAMESPACE *val, int64_t fallback) {
    if (any_is_int(val)) {
        return val->data.i;
    }
    return fallback;
}


static inline uint64_t any_as_uint_or(const ANY_NAMESPACE *val, uint64_t fallback) {
    if (any_is_uint(val)) {
        return val->data.u;
    }
    return fallback;
}


static inline float any_as_float_or(const ANY_NAMESPACE *val, float fallback) {
    if (any_is_float(val)) {
        return val->data.f;
    }
    return fallback;
}


static inline double any_as_double_or(const ANY_NAMESPACE *val, double fallback) {
    if (any_is_double(val)) {
        return val->data.d;
    }
    return fallback;
}


static inline const char *any_as_string_or(const ANY_NAMESPACE *val, const char *fallback) {
    if (val == NULL) {
        return fallback;
    } else if (val->type == TYPE_STRING) {
        return val->data.s;
    } else if (val->_type_sso == TYPE_STRING_SMALL) {
        return val->small_buf;
    }
    return fallback;
}


static inline Blob any_as_blob_or(ANY_NAMESPACE *val, Blob fallback) {
    if (val == NULL) {
        return fallback;
    } else if (val->type == TYPE_BLOB) {
        return *val->data.l;
    } else if (val->_type_sbo == TYPE_BLOB_SMALL) {
        Blob temp;
        temp.ptr = (uint8_t *) val->small_blob;
        temp.size = val->len;
        return temp;
    }
    return fallback;
}


static inline DynamicArray *any_as_array_or(const ANY_NAMESPACE *val, DynamicArray *fallback) {
    if (any_is_array(val)) {
        return val->data.a;
    }
    return fallback;
}


static inline HashMap *any_as_map_or(const ANY_NAMESPACE *val, HashMap *fallback) {
    if (any_is_map(val)) {
        return val->data.m;
    }
    return fallback;
}


static inline void *any_as_ptr_or(const ANY_NAMESPACE *val, void *fallback) {
    if (any_is_ptr(val)) {
        return val->data.p;
    }
    return fallback;
}


static inline anyarr_result any_free(DynamicArray *buf);

static inline anyarr_result map_free(HashMap *m);

static inline anyarr_result any_destroy(ANY_NAMESPACE *val) {
    if (val == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type == TYPE_STRING) {
        free(val->data.s);
    }
    if (val->type == TYPE_BLOB) {
        free(val->data.l->ptr);
        free(val->data.l);
    }
    if (val->type == TYPE_ARRAY) {
        any_free(val->data.a);
        free(val->data.a);
    }
    if (val->type == TYPE_MAP) {
        map_free(val->data.m);
        free(val->data.m);
    }
    *val = any_make_null();
    return ANYARR_OK;
}


static inline anyarr_result any_reassign(ANY_NAMESPACE *target, const ANY_NAMESPACE new_val) {
    if (target == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    any_destroy(target);
    *target = new_val;
    return ANYARR_OK;
}


static inline uint64_t map_hash(const char *key) {
    uint64_t hash = 14695981039346656037ULL;
    while (*key) {
        hash ^= (uint64_t) (unsigned char) (*key++);
        hash *= 1099511628211ULL;
    }
    return hash;
}


static inline anyarr_result any_init(DynamicArray *buf) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    buf->size = 0;
    buf->capacity = 4;
    buf->data = calloc(buf->capacity, sizeof(ANY_NAMESPACE));
    if (buf->data == NULL) {
        buf->capacity = 0;
        return ANYARR_ERR_OOM;
    }
    return ANYARR_OK;
}


static inline anyarr_result map_init(HashMap *m) {
    if (m == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    m->size = 0;
    m->deleted_count = 0;
    m->capacity = 16;
    m->entries = calloc(m->capacity, sizeof(MapEntry));
    if (m->entries == NULL) {
        m->capacity = 0;
        return ANYARR_ERR_OOM;
    }
    return ANYARR_OK;
}


static inline anyarr_result map_resize(HashMap *m);

static inline anyarr_result map_put(HashMap *m, const char *key, const ANY_NAMESPACE value) {
    if (m == NULL || m->entries == NULL || key == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if ((m->size + m->deleted_count) >= (m->capacity * 3) / 4) {
        const anyarr_result result = map_resize(m);
        if (result != ANYARR_OK) {
            return result;
        }
    }
    const uint64_t hash = map_hash(key);
    size_t index = hash % m->capacity;
    size_t dead_index = SIZE_MAX;
    while (1) {
        MapEntry *slot = &m->entries[index];
        if (slot->state == MAP_SLOT_EMPTY) {
            size_t insert_index;
            if (dead_index != SIZE_MAX) {
                insert_index = dead_index;
                m->deleted_count--;
            } else {
                insert_index = index;
            }
            MapEntry *target = &m->entries[insert_index];
            const size_t key_len = strlen(key);
            target->key = malloc(key_len + 1);
            if (target->key == NULL) {
                return ANYARR_ERR_OOM;
            }
            memcpy(target->key, key, key_len + 1);
            target->value = value;
            target->state = MAP_SLOT_OCCUPIED;
            m->size++;
            return ANYARR_OK;
        }
        if (slot->state == MAP_SLOT_OCCUPIED && strcmp(slot->key, key) == 0) {
            any_destroy(&slot->value);
            slot->value = value;
            return ANYARR_OK;
        }
        if (slot->state == MAP_SLOT_DELETED && dead_index == SIZE_MAX) {
            dead_index = index;
        }
        index = (index + 1) % m->capacity;
    }
}


static inline anyarr_result map_resize(HashMap *m) {
    if (m == NULL || m->entries == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    MapEntry *old_entries = m->entries;
    const size_t old_capacity = m->capacity;
    m->capacity = old_capacity << 1;
    m->entries = calloc(m->capacity, sizeof(MapEntry));
    if (m->entries == NULL) {
        m->entries = old_entries;
        m->capacity = old_capacity;
        return ANYARR_ERR_OOM;
    }
    m->deleted_count = 0;
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].state == MAP_SLOT_OCCUPIED) {
            const uint64_t hash = map_hash(old_entries[i].key);
            size_t index = hash % m->capacity;
            while (m->entries[index].state != MAP_SLOT_EMPTY) {
                index = (index + 1) % m->capacity;
            }
            m->entries[index].key = old_entries[i].key;
            m->entries[index].value = old_entries[i].value;
            m->entries[index].state = MAP_SLOT_OCCUPIED;
        }
    }
    free(old_entries);
    return ANYARR_OK;
}


static inline anyarr_result map_get(HashMap *m, const char *key, ANY_NAMESPACE **out_value) {
    if (m == NULL || m->entries == NULL || key == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    const uint64_t hash = map_hash(key);
    size_t index = hash % m->capacity;
    while (1) {
        MapEntry *slot = &m->entries[index];
        if (slot->state == MAP_SLOT_EMPTY) {
            return ANYARR_ERR_EMPTY;
        }
        if (slot->state == MAP_SLOT_DELETED) {
            index = (index + 1) % m->capacity;
            continue;
        }
        if (slot->state == MAP_SLOT_OCCUPIED && strcmp(slot->key, key) == 0) {
            *out_value = &slot->value;
            return ANYARR_OK;
        }
        index = (index + 1) % m->capacity;
    }
    return ANYARR_ERR_OUT_OF_BOUNDS;
}


static inline anyarr_result map_remove(HashMap *m, const char *key) {
    if (m == NULL || m->entries == NULL || key == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    const uint64_t hash = map_hash(key);
    size_t index = hash % m->capacity;
    const size_t start = index;
    do {
        MapEntry *slot = &m->entries[index];
        if (slot->state == MAP_SLOT_EMPTY) {
            break;
        }
        if (slot->state == MAP_SLOT_OCCUPIED && strcmp(slot->key, key) == 0) {
            any_destroy(&slot->value);
            free(slot->key);
            slot->state = MAP_SLOT_DELETED;
            m->size--;
            m->deleted_count++;
            return ANYARR_OK;
        }
        index = (index + 1) % m->capacity;
    } while (index != start);
    return ANYARR_ERR_OUT_OF_BOUNDS;
}


static inline anyarr_result any_append(DynamicArray *buf, const ANY_NAMESPACE value) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (buf->size == buf->capacity) {
        size_t new_capacity;
        if (buf->capacity == 0) {
            new_capacity = 4;
        } else {
            new_capacity = buf->capacity + (buf->capacity >> 1);
        }
        ANY_NAMESPACE *temp = realloc(buf->data, new_capacity * sizeof(ANY_NAMESPACE));
        if (temp == NULL) {
            return ANYARR_ERR_OOM;
        }
        buf->data = temp;
        buf->capacity = new_capacity;
    }
    buf->data[buf->size++] = value;
    return ANYARR_OK;
}


static inline anyarr_result any_remove_index(DynamicArray *buf, size_t index) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (index >= buf->size) {
        return ANYARR_ERR_OUT_OF_BOUNDS;
    }
    any_destroy(&buf->data[index]);
    const size_t index_queue = buf->size - index - 1;
    if (index_queue > 0) {
        memmove(&buf->data[index], &buf->data[index + 1], index_queue * sizeof(ANY_NAMESPACE));
    }
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result any_set_index(DynamicArray *buf, const size_t index, const ANY_NAMESPACE value) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (index >= buf->size) {
        return ANYARR_ERR_OUT_OF_BOUNDS;
    }
    any_destroy(&buf->data[index]);
    buf->data[index] = value;
    return ANYARR_OK;
}


static inline anyarr_result any_reserve(DynamicArray *buf, size_t new_capacity) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (new_capacity <= buf->capacity) {
        return ANYARR_OK;
    }
    ANY_NAMESPACE *temp = realloc(buf->data, new_capacity * sizeof(ANY_NAMESPACE));
    if (temp == NULL) {
        return ANYARR_ERR_OOM;
    }
    buf->data = temp;
    buf->capacity = new_capacity;
    return ANYARR_OK;
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-bool-conversion"
static inline anyarr_result any_clone(ANY_NAMESPACE *src, ANY_NAMESPACE *dest) {
    if (src == NULL || dest == NULL) {
        return ANYARR_ERR_NULLPTR;
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
            if (dest->type == TYPE_NULL) {
                return ANYARR_ERR_OOM;
            }
            return ANYARR_OK;
        case TYPE_ARRAY: {
            DynamicArray *src_arr = src->data.a;
            DynamicArray *new_arr = malloc(sizeof(DynamicArray));
            if (new_arr == NULL) {
                return ANYARR_ERR_OOM;
            }
            if (any_init(new_arr) != ANYARR_OK) {
                free(new_arr);
                return ANYARR_ERR_OOM;
            }
            for (size_t i = 0; i < src_arr->size; i++) {
                Any cloned_elem;
                anyarr_result res = any_clone(&src_arr->data[i], &cloned_elem);
                if (res != ANYARR_OK) {
                    any_free(new_arr);
                    free(new_arr);
                    return res;
                }
                any_append(new_arr, cloned_elem);
            }
            *dest = assign_array(new_arr);
            return ANYARR_OK;
        }
        case TYPE_PTR:
            *dest = assign_ptr(src->data.p); // Caller owns the lifetime
            return ANYARR_OK;
        case TYPE_BLOB: {
            Blob b;
            any_get_blob(src, &b);
            *dest = assign_blob(&b);
            if (dest->type == TYPE_NULL) {
                return ANYARR_ERR_OOM;
            }
            return ANYARR_OK;
        }
        case TYPE_MAP: {
            HashMap *src_map = src->data.m;
            HashMap *new_map = malloc(sizeof(HashMap));
            if (new_map == NULL) {
                return ANYARR_ERR_OOM;
            }
            if (map_init(new_map) != ANYARR_OK) {
                free(new_map);
                return ANYARR_ERR_OOM;
            }
            for (size_t i = 0; i < src_map->capacity; i++) {
                if (src_map->entries[i].state != MAP_SLOT_OCCUPIED)
                    continue;
                Any cloned_val;
                anyarr_result res = any_clone(&src_map->entries[i].value, &cloned_val);
                if (res != ANYARR_OK) {
                    map_free(new_map);
                    free(new_map);
                    return res;
                }
                map_put(new_map, src_map->entries[i].key, cloned_val);
            }
            *dest = assign_map(new_map);
            return ANYARR_OK;
        }
    }
    return ANYARR_ERR_TYPE_MISMATCH;
}


static inline anyarr_result any_equal(ANY_NAMESPACE *a, ANY_NAMESPACE *b) {
    if (a == NULL || b == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (any_is_string(a) && any_is_string(b)) {
        const char *sa = any_as_string_or(a, NULL);
        const char *sb = any_as_string_or(b, NULL);
        if (strcmp(sa, sb) == 0) {
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
        case TYPE_BLOB:
        case TYPE_BLOB_SMALL: {
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
        case TYPE_ARRAY: {
            DynamicArray *aa = a->data.a;
            DynamicArray *ab = b->data.a;
            if (aa->size != ab->size) {
                return ANYARR_NOT_EQUAL;
            }
            for (size_t i = 0; i < aa->size; i++) {
                anyarr_result res = any_equal(&aa->data[i], &ab->data[i]);
                if (res != ANYARR_EQUAL) {
                    return res;
                }
            }
            return ANYARR_EQUAL;
        }
        case TYPE_MAP: {
            HashMap *ma = a->data.m;
            HashMap *mb = b->data.m;
            if (ma->size != mb->size) {
                return ANYARR_NOT_EQUAL;
            }
            for (size_t i = 0; i < ma->capacity; i++) {
                if (ma->entries[i].state != MAP_SLOT_OCCUPIED) {
                    continue;
                }
                ANY_NAMESPACE *val_b;
                if (map_get(mb, ma->entries[i].key, &val_b) != ANYARR_OK) {
                    return ANYARR_NOT_EQUAL;
                }
                anyarr_result res = any_equal(&ma->entries[i].value, val_b);
                if (res != ANYARR_EQUAL) {
                    return res;
                }
            }
            return ANYARR_EQUAL;
        }
        case TYPE_STRING:
        case TYPE_STRING_SMALL:
            return ANYARR_EQUAL;
    }
    return ANYARR_NOT_EQUAL;
}


static inline anyarr_result any_shrink_to_fit(DynamicArray *buf) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (buf->size == 0) {
        free(buf->data);
        buf->data = NULL;
        buf->capacity = 0;
        return ANYARR_OK;
    }
    ANY_NAMESPACE *temp = realloc(buf->data, buf->size * sizeof(ANY_NAMESPACE));
    if (temp == NULL) {
        return ANYARR_ERR_OOM;
    }
    buf->data = temp;
    buf->capacity = buf->size;
    return ANYARR_OK;
}


static inline anyarr_result any_pop(DynamicArray *buf) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (buf->size == 0) {
        return ANYARR_ERR_EMPTY;
    }
    any_destroy(&buf->data[buf->size - 1]);
    buf->size--;
    return ANYARR_OK;
}


static inline anyarr_result any_clear(DynamicArray *buf) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    for (size_t i = 0; i < buf->size; i++) {
        any_destroy(&buf->data[i]);
    }
    buf->size = 0;
    return ANYARR_OK;
}


static inline anyarr_result any_free(DynamicArray *buf) {
    if (buf == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    for (size_t i = 0; i < buf->size; i++) {
        any_destroy(&buf->data[i]);
    }
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
    return ANYARR_OK;
}


static inline anyarr_result map_free(HashMap *m) {
    if (m == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].state == MAP_SLOT_OCCUPIED) {
            free(m->entries[i].key);
            any_destroy(&m->entries[i].value);
        }
    }
    free(m->entries);
    m->entries = NULL;
    m->size = 0;
    m->capacity = 0;
    return ANYARR_OK;
}


static inline const ANY_NAMESPACE *any_at(const DynamicArray *buf, size_t idx) {
    if (buf == NULL || idx >= buf->size) {
        return NULL;
    }
    return &buf->data[idx];
}


#define assign_any(x) _Generic((x), \
    _Bool: assign_bool,             \
    char: assign_char,              \
    signed char: assign_int,        \
    short: assign_int,              \
    int: assign_int,                \
    long: assign_int,               \
    long long: assign_int,          \
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
    DynamicArray*: assign_array,    \
    HashMap*: assign_map,           \
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
    DynamicArray**: any_get_arr,                        \
    HashMap**: any_get_map,                             \
    void**: any_get_ptr                                 \
)(val_ptr, out_ptr)

#define get_at(buf_ptr, index, out_ptr) \
get_any(any_at((buf_ptr), (index)), (out_ptr))

#define update_any(target_ptr, val) any_reassign((target_ptr), assign_any(val))


#endif

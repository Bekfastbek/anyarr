/*
 * This library should work with C11 onwards flawlessly but C99 needs a lot of setup to work and generally not recommended
 * To make it work on C99, you might need to use the GNU C99 standard instead of strict C99 for anonymous structs to work
 * There is also no support for generic macros so remove the macros at the bottom of the file and stick with specific datatypes
*/

#ifndef ANYARR_H
#define ANYARR_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*TO DO:
 * Making example code and writing docs
 * Flat Open Address Hashmaps implementation
 * Arena allocator for O(1) alloc
 * any_clone() and any_equal() for safe assignments and comparisons
 * any_iteration()? for looping through the arrays
 * Blob types for storing streams of data like b64 or tls
 * get_map() and get_array() for nested traversal
 * Synthetic benchmarks compared to other libraries in AWS
 */


#ifndef ANY_NAMESPACE // just a QOL feature, they can rename "Any" to prevent namespace pollution
#define ANY_NAMESPACE Any
#endif

typedef enum {
    ANYARR_OK,
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
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_MAP
};

typedef struct DynamicArray DynamicArray;

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
                void *p;
                DynamicArray *a;
            } data;
        };
        struct {
            uint8_t _type_alias;
            char small_buf[15];
            // From my testing 15 seemed to be much faster than 16 bytes around ~30% on append speeds, going above introduces the next set of bytes in l1 cache which means more padding and more work for cpu
        };
    };
} ANY_NAMESPACE;

struct DynamicArray {
    ANY_NAMESPACE *data;
    size_t size;
    size_t capacity;
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
        ANY_NAMESPACE val = {._type_alias = TYPE_STRING_SMALL};
        strcpy(val.small_buf, s);
        return val;
    }
    char *dup = malloc(len + 1);
    if (dup == NULL) {
        return (ANY_NAMESPACE){ANYARR_ERR_OOM};
    }
    strcpy(dup, s);
    return (ANY_NAMESPACE){.type = TYPE_STRING, .data.s = dup};
}


static inline ANY_NAMESPACE assign_ptr(void *p) {
    if (p == NULL) {
        return any_make_null();
    }
    return (ANY_NAMESPACE){TYPE_PTR, .data.p = p};
}


static inline ANY_NAMESPACE assign_array(DynamicArray *a) {
    if (a == NULL) {
        return any_make_null();
    }
    return (ANY_NAMESPACE){TYPE_ARRAY, .data.a = a};
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


static inline _Bool any_is_ptr(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_PTR;
}


static inline anyarr_result any_get_bool(const ANY_NAMESPACE *val, _Bool *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_BOOL) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.b;
    return ANYARR_OK;
}


static inline anyarr_result any_get_char(const ANY_NAMESPACE *val, char *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_CHAR) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.c;
    return ANYARR_OK;
}


static inline anyarr_result any_get_int(const ANY_NAMESPACE *val, int64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_INT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.i;
    return ANYARR_OK;
}


static inline anyarr_result any_get_uint(const ANY_NAMESPACE *val, uint64_t *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_UINT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.u;
    return ANYARR_OK;
}


static inline anyarr_result any_get_float(const ANY_NAMESPACE *val, float *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_FLOAT) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.f;
    return ANYARR_OK;
}


static inline anyarr_result any_get_double(const ANY_NAMESPACE *val, double *out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_DOUBLE) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.d;
    return ANYARR_OK;
}


static inline anyarr_result any_get_string(const ANY_NAMESPACE *val, const char **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type == TYPE_STRING) {
        *out_value = val->data.s;
        return ANYARR_OK;
    } else if (val->type == TYPE_STRING_SMALL) {
        *out_value = val->small_buf;
        return ANYARR_OK;
    }
    return ANYARR_ERR_TYPE_MISMATCH;
}


static inline anyarr_result any_get_ptr(const ANY_NAMESPACE *val, void **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_PTR) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.p;
    return ANYARR_OK;
}


static inline anyarr_result any_get_arr(const ANY_NAMESPACE *val, DynamicArray **out_value) {
    if (val == NULL || out_value == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type != TYPE_ARRAY) {
        return ANYARR_ERR_TYPE_MISMATCH;
    }
    *out_value = val->data.a;
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
    if (!val) {
        return fallback;
    }
    if (val->type == TYPE_STRING) {
        return val->data.s;
    }
    if (val->type == TYPE_STRING_SMALL) {
        return val->small_buf;
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

static inline anyarr_result any_destroy(ANY_NAMESPACE *val) {
    if (val == NULL) {
        return ANYARR_ERR_NULLPTR;
    }
    if (val->type == TYPE_STRING) {
        free(val->data.s);
    }
    if (val->type == TYPE_ARRAY) {
        any_free(val->data.a);
        free(val->data.a);
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
    DynamicArray*: assign_array,    \
    default: assign_ptr             \
)(x)

#define get_any(val_ptr, out_ptr) _Generic((out_ptr),   \
    _Bool*: any_get_bool,                                \
    char*: any_get_char,                                \
    int64_t*: any_get_int,                              \
    uint64_t*: any_get_uint,                            \
    float*: any_get_float,                              \
    double*: any_get_double,                            \
    const char**: any_get_string,                       \
    void**: any_get_ptr                                 \
)(val_ptr, out_ptr)

#define get_at(buf_ptr, index, out_ptr) \
get_any(any_at((buf_ptr), (index)), (out_ptr))

#define update_any(target_ptr, val) any_reassign((target_ptr), assign_any(val))


#endif

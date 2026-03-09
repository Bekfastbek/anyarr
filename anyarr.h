/*
 * This library should work with C11 onwards flawlessly but C99 needs a lot of setup to work and generally not recommended
 * To make it work on C99, you might need to use the GNU C99 standard instead of strict C99 for anonymous structs to work
 * There is also no support for generic macros so remove the macros at the bottom of the file and stick with specific datatypes
*/

#ifndef ANYARR_H
#define ANYARR_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>



enum Type {
    TYPE_NULL,
    TYPE_ERROR_OOM,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_STRING_SMALL
};

typedef struct {
    union {
        struct {
            uint8_t type;
            union {
                bool b;
                char c;
                int i;
                float f;
                double d;
                char *s;
            } data;
        };
        struct {
            uint8_t _type_alias;
            char small_buf[15]; // From my testing 15 seemed to be much faster than 16 bytes around ~30% on append speeds, going below 15 bytes didn't change anything in terms of perf
        };
    };
} any;

typedef struct {
    any *data;
    size_t size;
    size_t capacity;
} DynamicArray;



static inline any assign_bool(const bool b) {
    return (any){TYPE_BOOL, .data.b = b};
}



static inline any assign_char(const char c) {
    return (any){TYPE_CHAR, .data.c = c};
}



static inline any assign_int(const int i) {
    return (any){TYPE_INT, .data.i = i};
}



static inline any assign_float(const float f) {
    return (any){TYPE_FLOAT, .data.f = f};
}



static inline any assign_double(const double d) {
    return (any){TYPE_DOUBLE, .data.d = d};
}



static inline any assign_string(const char *s) {
    if (s == NULL) {
        return (any){TYPE_NULL};
    }
    const size_t len = strlen(s);
    if (len < 15) {
        any val = { ._type_alias = TYPE_STRING_SMALL };
        strcpy(val.small_buf, s);
        return val;
    }
    char *dup = malloc(len + 1);
    if (dup == NULL) {
        return (any){TYPE_ERROR_OOM};
    }
    strcpy(dup, s);
    return (any){ .type = TYPE_STRING, .data.s = dup };
}



static inline bool any_is_null(const any *val) {
    return val && val->type == TYPE_NULL;
}



static inline bool any_is_bool(const any *val) {
    return val && val->type == TYPE_BOOL;
}



static inline bool any_is_char(const any *val) {
    return val && val->type == TYPE_CHAR;
}



static inline bool any_is_int(const any *val) {
    return val && val->type == TYPE_INT;
}



static inline bool any_is_float(const any *val) {
    return val && val->type == TYPE_FLOAT;
}



static inline bool any_is_double(const any *val) {
    return val && val->type == TYPE_DOUBLE;
}



static inline bool any_is_string(const any *val) {
    return val && (val->type == TYPE_STRING || val->type == TYPE_STRING_SMALL);
}



static inline bool any_get_bool(const any *val, bool *out_value) {
    if (val == NULL || out_value == NULL || val->type != TYPE_BOOL) {
        return false;
    }
    *out_value = val->data.b;
    return true;
}



static inline bool any_get_char(const any *val, char *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_CHAR) {
        return false;
    }
    *out_value = val -> data.c;
    return true;
}



static inline bool any_get_int(const any *val, int *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_INT) {
        return false;
    }
    *out_value = val -> data.i;
    return true;
}



static inline bool any_get_float(const any *val, float *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_FLOAT) {
        return false;
    }
    *out_value = val -> data.f;
    return true;
}



static inline bool any_get_double(const any *val, double *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_DOUBLE) {
        return false;
    }
    *out_value = val -> data.d;
    return true;
}



static inline bool any_get_string(const any *val, const char **out_value) {
    if (val == NULL || out_value == NULL) {
        return false;
    }
    if (val -> type == TYPE_STRING) {
        *out_value = val -> data.s;
        return true;
    } else if (val -> type == TYPE_STRING_SMALL) {
        *out_value = val -> small_buf;
        return true;
    }
    return false;
}



static inline bool any_as_bool_or(const any *val, bool fallback) {
    if (any_is_bool(val)) {
        return val->data.b;
    }
    return fallback;
}



static inline char any_as_char_or(const any *val, char fallback) {
    if (any_is_char(val)) {
        return val->data.c;
    }
    return fallback;
}



static inline int any_as_int_or(const any *val, int fallback) {
    if (any_is_int(val)) {
        return val->data.i;
    }
    return fallback;
}



static inline float any_as_float_or(const any *val, float fallback) {
    if (any_is_float(val)) {
        return val->data.f;
    }
    return fallback;
}



static inline double any_as_double_or(const any *val, double fallback) {
    if (any_is_double(val)) {
        return val->data.d;
    }
    return fallback;
}



static inline const char* any_as_string_or(const any *val, const char* fallback) {
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



static inline any any_make_null() {
    return (any){TYPE_NULL};
}



static inline void any_destroy(any *val) {
    if (val == NULL) return;
    if (val->type == TYPE_STRING) {
        free(val->data.s);
    }
    *val = any_make_null();
}



static inline void any_reassign(any *target, const any new_val) {
    if (target == NULL) {
        return;
    }
    any_destroy(target);
    *target = new_val;
}



static inline bool any_init(DynamicArray *buf) {
    if (buf == NULL) {
        return false;
    }
    buf -> size = 0;
    buf -> capacity = 4;
    buf -> data = calloc(buf -> capacity, sizeof(any));
    if (buf -> data == NULL) {
        buf -> capacity = 0;
        return false;
    }
    return true;
}



static inline bool any_append(DynamicArray *buf, const any value) {
    if (buf == NULL) {
        return false;
    }
    if (buf->size == buf->capacity) {
        size_t new_capacity;
        if (buf->capacity == 0) {
            new_capacity = 4;
        }
        else {
            new_capacity = buf->capacity + (buf->capacity >> 1);
        }
        any *temp = realloc(buf->data, new_capacity * sizeof(any));
        if (temp == NULL) {
            return false;
        }
        buf->data = temp;
        buf->capacity = new_capacity;
    }
    buf->data[buf->size++] = value;
    return true;
}



static inline bool any_remove_index(DynamicArray *buf, size_t index) {
    if (buf == NULL || index >= buf -> size) {
        return false;
    }
    any_destroy(&buf -> data[index]);
    const size_t index_queue = buf -> size - index - 1;
    if (index_queue > 0) {
        memmove(&buf -> data[index], &buf -> data[index + 1], index_queue * sizeof(any));
    }
    buf -> size--;
    return true;
}



static inline bool any_set_index(DynamicArray *buf, const size_t index, const any value) {
    if (buf == NULL || index >= buf -> size) {
        return false;
    }
    any_destroy(&buf -> data[index]);
    buf -> data[index] = value;
    return true;
}



static inline bool any_reserve(DynamicArray *buf, size_t new_capacity) {
    if (buf == NULL || new_capacity <= buf->capacity) {
        return true;
    }
    any *temp = realloc(buf->data, new_capacity * sizeof(any));
    if (temp == NULL) {
        return false;
    }
    buf->data = temp;
    buf->capacity = new_capacity;
    return true;
}



static inline bool any_shrink_to_fit(DynamicArray *buf) {
    if (buf == NULL || buf->capacity == buf->size) {
        return false;
    }
    if (buf->size == 0) {
        free(buf->data);
        buf->data = NULL;
        buf->capacity = 0;
        return true;
    }
    any *temp = realloc(buf->data, buf->size * sizeof(any));
    if (temp == NULL) return false;
    buf->data = temp;
    buf->capacity = buf->size;
    return true;
}



static inline bool any_pop(DynamicArray *buf) {
    if (buf == NULL || buf->size == 0) {
        return false;
    }
    any_destroy(&buf->data[buf->size - 1]);
    buf->size--;
    return true;
}



static inline void any_clear(DynamicArray *buf) {
    if (buf == NULL || buf->data == NULL) {
        return;
    }
    for (size_t i = 0; i < buf->size; i++) {
        any_destroy(&buf->data[i]);
    }
    buf->size = 0;
}



static inline void any_free(DynamicArray *buf) {
    if (buf == NULL || buf -> data == NULL) {
        return;
    }
    for (size_t i = 0; i < buf -> size; i++) {
        any_destroy(&buf -> data[i]);
    }
    free(buf -> data);
    buf -> data = NULL;
    buf -> size = 0;
    buf -> capacity = 0;
}



static inline const any *any_at(const DynamicArray *buf, size_t idx) {
    if (buf == NULL || idx >= buf->size) {
        return NULL;
    }
    return &buf->data[idx];
}



#define assign_any(x) _Generic((x), \
    _Bool: assign_bool,             \
    char: assign_char,              \
    int: assign_int,                \
    float: assign_float,            \
    double: assign_double,          \
    char*: assign_string,           \
    const char*: assign_string      \
)(x)

#define get_any(val_ptr, out_ptr) _Generic((out_ptr),   \
    bool*: any_get_bool,                                \
    char*: any_get_char,                                \
    int*: any_get_int,                                  \
    float*: any_get_float,                              \
    double*: any_get_double,                            \
    const char**: any_get_string                        \
)(val_ptr, out_ptr)

#define get_at(buf_ptr, index, out_ptr) \
get_any(any_at((buf_ptr), (index)), (out_ptr))

#define update_any(target_ptr, val) any_reassign((target_ptr), assign_any(val))



#endif
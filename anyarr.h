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



/*TO DO:
 * Proper error handling instead of just using bool
 * Making example code and writing docs
 * assign_int() needs to support short, long long, unsigned, etc
 */



#ifndef ANY_NAMESPACE // just a QOL feature, they can rename "Any" to prevent namespace pollution
#define ANY_NAMESPACE Any
#endif

typedef enum {
    ANYARR_OK,
    ANYARR_OOM,
    ANYARR_OVERFLOW
} anyarr_result;

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
            char small_buf[15]; // From my testing 15 seemed to be much faster than 16 bytes around ~30% on append speeds, going above introduces the next set of bytes which means more padding and more work for cpu
        };
    };
} ANY_NAMESPACE;

typedef struct {
    ANY_NAMESPACE *data;
    size_t size;
    size_t capacity;
} DynamicArray;



static inline ANY_NAMESPACE assign_bool(const bool b) {
    return (ANY_NAMESPACE){TYPE_BOOL, .data.b = b};
}



static inline ANY_NAMESPACE assign_char(const char c) {
    return (ANY_NAMESPACE){TYPE_CHAR, .data.c = c};
}



static inline ANY_NAMESPACE assign_int(const int i) {
    return (ANY_NAMESPACE){TYPE_INT, .data.i = i};
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
        ANY_NAMESPACE val = { ._type_alias = TYPE_STRING_SMALL };
        strcpy(val.small_buf, s);
        return val;
    }
    char *dup = malloc(len + 1);
    if (dup == NULL) {
        return (ANY_NAMESPACE){TYPE_ERROR_OOM};
    }
    strcpy(dup, s);
    return (ANY_NAMESPACE){ .type = TYPE_STRING, .data.s = dup };
}



static inline bool any_is_null(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_NULL;
}



static inline bool any_is_bool(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_BOOL;
}



static inline bool any_is_char(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_CHAR;
}



static inline bool any_is_int(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_INT;
}



static inline bool any_is_float(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_FLOAT;
}



static inline bool any_is_double(const ANY_NAMESPACE *val) {
    return val && val->type == TYPE_DOUBLE;
}



static inline bool any_is_string(const ANY_NAMESPACE *val) {
    return val && (val->type == TYPE_STRING || val->type == TYPE_STRING_SMALL);
}



static inline bool any_get_bool(const ANY_NAMESPACE *val, bool *out_value) {
    if (val == NULL || out_value == NULL || val->type != TYPE_BOOL) {
        return false;
    }
    *out_value = val->data.b;
    return true;
}



static inline bool any_get_char(const ANY_NAMESPACE *val, char *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_CHAR) {
        return false;
    }
    *out_value = val -> data.c;
    return true;
}



static inline bool any_get_int(const ANY_NAMESPACE *val, int *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_INT) {
        return false;
    }
    *out_value = val -> data.i;
    return true;
}



static inline bool any_get_float(const ANY_NAMESPACE *val, float *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_FLOAT) {
        return false;
    }
    *out_value = val -> data.f;
    return true;
}



static inline bool any_get_double(const ANY_NAMESPACE *val, double *out_value) {
    if (val == NULL || out_value == NULL || val -> type != TYPE_DOUBLE) {
        return false;
    }
    *out_value = val -> data.d;
    return true;
}



static inline bool any_get_string(const ANY_NAMESPACE *val, const char **out_value) {
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



static inline bool any_as_bool_or(const ANY_NAMESPACE *val, bool fallback) {
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



static inline int any_as_int_or(const ANY_NAMESPACE *val, int fallback) {
    if (any_is_int(val)) {
        return val->data.i;
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



static inline const char* any_as_string_or(const ANY_NAMESPACE *val, const char* fallback) {
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



static inline ANY_NAMESPACE any_make_null() {
    return (ANY_NAMESPACE){TYPE_NULL};
}



static inline void any_destroy(ANY_NAMESPACE *val) {
    if (val == NULL) return;
    if (val->type == TYPE_STRING) {
        free(val->data.s);
    }
    *val = any_make_null();
}



static inline void any_reassign(ANY_NAMESPACE *target, const ANY_NAMESPACE new_val) {
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
    buf -> data = calloc(buf -> capacity, sizeof(ANY_NAMESPACE));
    if (buf -> data == NULL) {
        buf -> capacity = 0;
        return false;
    }
    return true;
}



static inline bool any_append(DynamicArray *buf, const ANY_NAMESPACE value) {
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
        ANY_NAMESPACE *temp = realloc(buf->data, new_capacity * sizeof(ANY_NAMESPACE));
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
        memmove(&buf -> data[index], &buf -> data[index + 1], index_queue * sizeof(ANY_NAMESPACE));
    }
    buf -> size--;
    return true;
}



static inline bool any_set_index(DynamicArray *buf, const size_t index, const ANY_NAMESPACE value) {
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
    ANY_NAMESPACE *temp = realloc(buf->data, new_capacity * sizeof(ANY_NAMESPACE));
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
    ANY_NAMESPACE *temp = realloc(buf->data, buf->size * sizeof(ANY_NAMESPACE));
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



static inline const ANY_NAMESPACE *any_at(const DynamicArray *buf, size_t idx) {
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
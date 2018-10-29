// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#ifndef MJS_H
#define MJS_H

#if defined(__cplusplus)
extern "C" {
#endif

// Types
#define mjs vm                   // Aliasing `struct mjs` to `struct vm`
typedef unsigned int mjs_val_t;  // must be uint32_t
typedef enum { MJS_SUCCESS, MJS_FAILURE } mjs_err_t;
typedef enum {
  MJS_UNDEFINED,
  MJS_NULL,
  MJS_TRUE,
  MJS_FALSE,
  MJS_STRING,
  MJS_OBJECT,
  MJS_ARRAY,
  MJS_FUNCTION,
  MJS_NUMBER,
} mjs_type_t;

// Create/destroy
struct mjs *mjs_create(void);
void mjs_destroy(struct mjs *);

// Execute code
mjs_err_t mjs_exec(struct mjs *mjs, const char *buf, mjs_val_t *result);

// Inspect values
mjs_type_t mjs_type(mjs_val_t val);
float mjs_get_number(mjs_val_t v);
char *mjs_get_string(struct mjs *, mjs_val_t v, int *len);

#if defined(__cplusplus)
}
#endif

// VM tunables

#ifndef MJS_DATA_STACK_SIZE
#define MJS_DATA_STACK_SIZE 25
#endif

#ifndef MJS_CALL_STACK_SIZE
#define MJS_CALL_STACK_SIZE 25
#endif

#ifndef MJS_STRING_POOL_SIZE
#define MJS_STRING_POOL_SIZE 128
#endif

#ifndef MJS_OBJ_POOL_SIZE
#define MJS_OBJ_POOL_SIZE 10
#endif

#ifndef MJS_PROP_POOL_SIZE
#define MJS_PROP_POOL_SIZE 20
#endif

#ifndef MJS_ERROR_MESSAGE_SIZE
#define MJS_ERROR_MESSAGE_SIZE 50
#endif

#endif  // MJS_H

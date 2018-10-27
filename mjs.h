// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#ifndef MJS_H
#define MJS_H

#if defined(__cplusplus)
extern "C" {
#endif

#define mjs vm
typedef unsigned int mjs_val_t;
typedef enum { MJS_SUCCESS, MJS_ERROR } mjs_err_t;

struct mjs *mjs_create(void);
void mjs_destroy(struct mjs *);
mjs_err_t mjs_exec(struct mjs *mjs, const char *buf, mjs_val_t *result);

int mjs_is_number(mjs_val_t v);
int mjs_is_string(mjs_val_t v);
float mjs_get_number(mjs_val_t v);
char *mjs_get_string(struct mjs *, mjs_val_t v, int *len);

#if defined(__cplusplus)
}
#endif

#endif  // MJS_H

// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#ifndef MJS_H
#define MJS_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef float mjs_val_t;
typedef enum { MJS_OK, MJS_ERR } mjs_err_t;

struct mjs {
  char err_msg[20];
  mjs_val_t stack[50];
  int sp;
};

mjs_err_t mjs_exec(struct mjs *mjs, const char *buf);

#if defined(__cplusplus)
}
#endif

#endif  // MJS_H

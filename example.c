// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "mjs.c"

#include <stdio.h>
#include <stdlib.h>

static float sub(float a, float b) {
  return a - b;
}

static int add(int a, int b) {
  return a + b;
}

static float pi(void) {
  return 3.1415926;
}

static char *fmt(const char *fmt, float f) {  // Format float value
  static char buf[20];
  snprintf(buf, sizeof(buf), fmt, f);
  printf("PPPP [%s], %f, [%s]\n", fmt, f, buf);
  return buf;
}

int main(int argc, char *argv[]) {
  int i;
  struct mjs *mjs = mjs_create();
  val_t res = MJS_UNDEFINED;

  mjs_ffi(mjs, "sub", (cfn_t) sub, "fff");
  mjs_ffi(mjs, "add", (cfn_t) add, "iii");
  mjs_ffi(mjs, "fmt", (cfn_t) fmt, "ssf");
  mjs_ffi(mjs, "pi", (cfn_t) pi, "f");

  for (i = 1; i < argc && argv[i][0] == '-' && res != MJS_ERROR; i++) {
    if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
      const char *code = argv[++i];
      res = mjs_eval(mjs, code, -1);
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [-e js_expression]\n", argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Unknown flag: [%s]\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  printf("%s\n", mjs_stringify(mjs, res));
  mjs_destroy(mjs);
  return EXIT_SUCCESS;
}

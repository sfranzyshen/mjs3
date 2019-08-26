// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "mjs.c"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int i;
  struct mjs *mjs = mjs_create();
  val_t res = MJS_UNDEFINED;

  mjs_ffi(mjs, "str", (cfn_t) tostr, "smj");

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

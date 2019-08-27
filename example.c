// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "elk.c"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int i;
  struct elk *elk = js_create();
  jsval_t res = JS_UNDEFINED;

  js_ffi(elk, tostr, "smj");

  for (i = 1; i < argc && argv[i][0] == '-' && res != JS_ERROR; i++) {
    if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
      const char *code = argv[++i];
      res = js_eval(elk, code, -1);
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [-e js_expression]\n", argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Unknown flag: [%s]\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  printf("%s\n", js_stringify(elk, res));
  js_destroy(elk);
  return EXIT_SUCCESS;
}

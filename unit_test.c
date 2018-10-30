#include "mjs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check_num(mjs_val_t v, float expected) {
  return mjs_type(v) == MJS_NUMBER &&
         fabs(mjs_get_number(v) - expected) < 0.0001;
}

static int check_str(struct mjs *mjs, mjs_val_t v, const char *expected) {
  mjs_len_t n;
  return mjs_type(v) == MJS_STRING &&
         strcmp(mjs_get_string(mjs, v, &n), expected) == 0 &&
         n == strlen(expected);
}

static int check_code_num(struct mjs *mjs, const char *code, float expected) {
  mjs_val_t v;
  if (mjs_exec(mjs, code, strlen(code), &v) != MJS_SUCCESS) return 0;
  return check_num(v, expected);
}

static void test_expr(void) {
  struct mjs *mjs = mjs_create();
  assert(check_code_num(mjs, "1 + 2 * 3.7 - 7 % 3", 7.4f));
  assert(check_code_num(mjs, "let a = 1.23, b = 5.3;", 5.3f));
  assert(check_code_num(mjs, "a;", 1.23f));
  assert(check_code_num(mjs, "a - 2 * 3.1;", -4.97f));
  assert(check_code_num(mjs,
                        "let a = 1.23; a + 1; "
                        "let f = function(a) { return a + 1; };1;",
                        1));
  assert(check_code_num(mjs, "2 * (1 + 2)", 6.0f));
  mjs_destroy(mjs);
}

static void test_strings(void) {
  struct mjs *mjs = mjs_create();
  mjs_val_t v;
  assert(mjs_exec(mjs, "'a' + 'b'", 9, &v) == MJS_SUCCESS);
  assert(check_str(mjs, v, "ab"));
  mjs_destroy(mjs);
}

int main(void) {
  test_expr();
  test_strings();
  printf("TEST PASSED\n");
  return 0;
}

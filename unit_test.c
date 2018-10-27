#include <assert.h>
#include <math.h>
#include "mjs.h"

static int check_num(mjs_val_t v, float expected) {
  return mjs_is_number(v) && fabs(mjs_get_number(v) - expected) < 0.0001;
}

static void test_expr(void) {
  struct mjs *mjs = mjs_create();
  mjs_val_t v;
  assert(mjs_exec(mjs, "1 + 2 * 3.7 - 7 % 3", &v) == MJS_SUCCESS);
  assert(check_num(v, 7.4));
  assert(mjs_exec(mjs, "let a = 1.23, b = 5.3;", &v) == MJS_SUCCESS);
  assert(check_num(v, 5.3));
  assert(mjs_exec(mjs, "a;", &v) == MJS_SUCCESS);
  assert(check_num(v, 1.23));
  assert(mjs_exec(mjs, "a - 2 * 3.1;", &v) == MJS_SUCCESS);
  assert(check_num(v, -4.97));
}

int main(void) {
  test_expr();
  return 0;
}

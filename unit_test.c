#include <assert.h>
#include <math.h>
#include "mjs.h"

static void test_expr(void) {
  struct mjs *mjs = mjs_create();
  mjs_val_t v;
  assert(mjs_exec(mjs, "1 + 2 * 3.7 - 7 % 3", &v) == MJS_SUCCESS);
  assert(mjs_is_number(v) && fabs(mjs_get_number(v) - 7.4) < 0.001);
  // assert(mjs_exec(mjs, "1 ? 2: 3", &v) == MJS_SUCCESS);
  // assert(mjs_is_number(v) && fabs(mjs_get_number(v) - 2) < 0.001);
  // assert(mjs_exec(mjs, "let abc = 123;", &v) == MJS_SUCCESS);
  mjs_destroy(mjs);
};

int main(void) {
  test_expr();
  return 0;
}

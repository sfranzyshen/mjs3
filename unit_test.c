#include <assert.h>
#include "mjs.h"

static void test1(void) {
  struct mjs *mjs = mjs_create();
  mjs_val_t v;
  assert(mjs_exec(mjs, "1 + 2 * 3.7 - 7 % 3", &v) == MJS_SUCCESS);
  assert(mjs_is_number(v) && mjs_get_number(v) == 7.4f);
  mjs_destroy(mjs);
};

int main(void) {
  test1();
  return 0;
}

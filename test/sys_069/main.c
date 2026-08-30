#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // NULL vs empty string - NULL returns 0, distinct from "" (the djb2
  // seed value, 5381). Unlike most sys_string_* functions, NULL is not
  // treated as equivalent to an empty string here.

  test_assert(sys_hash_djb2(NULL) == 0);
  test_assert(sys_hash_djb2("") == 5381);

  ///////////////////////////////////////////////////////////////////////
  // Known djb2 reference values (seed 5381, hash = hash*33 + c) - short
  // enough to fit a 32-bit uintptr_t too, so these hold on every
  // platform this runs on, not just 64-bit ones.

  test_assert(sys_hash_djb2("a") == 177670);
  test_assert(sys_hash_djb2("abc") == 193485963u);

  ///////////////////////////////////////////////////////////////////////
  // Deterministic - the same string always hashes the same way.

  {
    uintptr_t h1 = sys_hash_djb2("picofuse");
    uintptr_t h2 = sys_hash_djb2("picofuse");
    test_assert(h1 == h2);
  }

  ///////////////////////////////////////////////////////////////////////
  // Case-sensitive, and different strings (including ones sharing a
  // prefix) hash differently - checked pairwise rather than against
  // hardcoded values, since these are long enough to wrap a 32-bit
  // uintptr_t differently than a 64-bit one.

  {
    uintptr_t hello = sys_hash_djb2("hello");
    uintptr_t Hello = sys_hash_djb2("Hello");
    uintptr_t hello_bang = sys_hash_djb2("hello!");
    uintptr_t picofuse = sys_hash_djb2("picofuse");
    uintptr_t world = sys_hash_djb2("world");

    test_assert(hello != Hello);      // case-sensitive
    test_assert(hello != hello_bang); // shared prefix, still distinct
    test_assert(hello != picofuse);
    test_assert(hello != world);
    test_assert(Hello != hello_bang);
    test_assert(Hello != picofuse);
    test_assert(Hello != world);
    test_assert(hello_bang != picofuse);
    test_assert(hello_bang != world);
    test_assert(picofuse != world);
  }

  sys_exit();
  return 0;
}

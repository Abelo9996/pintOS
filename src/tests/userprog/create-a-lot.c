/* Creates 16 ordinary empty files. */

#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  CHECK(create("f1.dat", 0), "create f1.dat");
  CHECK(create("f2.dat", 0), "create f2.dat");
  CHECK(create("f3.dat", 0), "create f3.dat");
  CHECK(create("f4.dat", 0), "create f4.dat");
  CHECK(create("f5.dat", 0), "create f5.dat");
  CHECK(create("f6.dat", 0), "create f6.dat");
  CHECK(create("f7.dat", 0), "create f7.dat");
  CHECK(create("f8.dat", 0), "create f8.dat");
  CHECK(create("f9.dat", 0), "create f9.dat");
  CHECK(create("f10.dat", 0), "create f10.dat");
}

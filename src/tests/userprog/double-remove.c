#include "tests/lib.h"
#include "tests/main.h"

test_main(void) {
  char* file_name = "f1.dat";

  CHECK(create("f1.dat", 0), "create %s", file_name);
  int fd = open("f1.dat");
  CHECK(remove(file_name), "remove %s", file_name);
  int fd2 = open("f1.dat");
  if (fd2 > 0) {
    msg("open failed");
  }
  bool remove_success = remove(file_name);
  if (!remove_success) {
    msg("%s deleted");
  }
}
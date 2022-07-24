/* Tries to get the filesize of an invalid fd, which must either fail silently
   or terminate with exit code -1. */

#include <syscall.h>
#include "tests/main.h"

void test_main(void) { filesize(0x20101234); }

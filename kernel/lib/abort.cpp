#include <yak/log.h>
#include <yak/panic.h>

extern "C" [[noreturn]] void abort() {
  yak::panic("abort!");
}

extern "C" void atexit() {
  pr_debug("atexit called\n");
}

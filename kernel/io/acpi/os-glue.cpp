#include "yak/vm/direct.h"
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <yak/arch-mm.h>
#include <yak/log.h>
#include <yak/panic.h>

namespace yak {

constexpr LogLevel toLogLevel(uacpi_log_level level) {
  switch (level) {
  case UACPI_LOG_DEBUG:
    return LogLevel::Debug;
  case UACPI_LOG_TRACE:
    return LogLevel::Trace;
  case UACPI_LOG_INFO:
    return LogLevel::Info;
  case UACPI_LOG_WARN:
    return LogLevel::Warn;
  case UACPI_LOG_ERROR:
    return LogLevel::Error;
  default:
    return LogLevel::Fail;
  }
}

extern "C" {

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *logmsg) {
  printk(toLogLevel(level), "uacpi: %s", logmsg);
}

void *uacpi_kernel_map(uacpi_phys_addr addr, [[maybe_unused]] uacpi_size len) {
  return (void *) (addr + arch::HHDM_BASE);
}

void uacpi_kernel_unmap([[maybe_unused]] void *addr,
                        [[maybe_unused]] uacpi_size len) {}

void *uacpi_kernel_alloc(uacpi_size size) {
  panic("todo!");
  return nullptr;
}

void uacpi_kernel_free(void *mem) {
  panic("todo!");
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
  panic("todo!");
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle,
                                        uacpi_u16 ms_timeout) {
  panic("todo!");
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
  panic("todo!");
}
}

} // namespace yak

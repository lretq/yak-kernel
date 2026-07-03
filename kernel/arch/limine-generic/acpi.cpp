#include <limine-generic/request.h>
#include <limine.h>
#include <uacpi/status.h>
#include <uacpi/uacpi.h>
#include <yak/arch-mm.h>
#include <yak/vm/address.h>

namespace yak {

extern "C" {
LIMINE_REQ static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
  auto res = rsdp_request.response;
  if (!res || !res->address)
    return UACPI_STATUS_NOT_FOUND;

  // For Limine Revisions != 3 the address is virtual
  *out_rsdp_address =
      (vaddr_t) rsdp_request.response->address - arch::HHDM_BASE;
  return UACPI_STATUS_OK;
}
}

} // namespace yak

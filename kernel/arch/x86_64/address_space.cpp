#include <cstdint>
#include <x86_64/asm.h>
#include <yak/panic.h>
#include <yak/vm/direct.h>

namespace yak::arch {
enum {
  CR4_LA57_BIT = (1UL << 12),
};

static int get_va_bits() {
  uint64_t cr4 = asm_rdcr4();
  return (cr4 & CR4_LA57_BIT) ? 57 : 48;
}

bool is_canonical(uintptr_t addr) {
  static int va_bits = 0;
  if (!va_bits)
    va_bits = get_va_bits();

  int shift = va_bits - 1;
  uint64_t sign_ext = addr >> shift;
  return (sign_ext == 0 || sign_ext == UINT64_MAX >> shift);
}

bool is_direct_mapped([[maybe_unused]] paddr_t pa,
                      [[maybe_unused]] size_t len) {
  return true;
}

vaddr_t direct_map_offset() {
  return arch::HHDM_BASE;
}

std::optional<vaddr_t> map_window([[maybe_unused]] paddr_t pa_page,
                                  [[maybe_unused]] size_t page_count) {
  panic("not implemented on this arch");
}

void unmap_window([[maybe_unused]] vaddr_t va,
                  [[maybe_unused]] size_t page_count) {
  panic("not implemented on this arch");
}
} // namespace yak::arch

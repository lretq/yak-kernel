#include <cstring>
#include <yak/panic.h>
#include <yak/util.h>
#include <yak/vm/direct.h>

namespace yak {
void zero_physical_memory(paddr_t addr, size_t size) {
  if (arch::is_direct_mapped(addr, size)) {
    auto window =
        expect(MapWindow::create(addr, size), "failed to create memory window");

    std::memset(window.data(), 0, size);
  } else {
    for (size_t i = 0; i < size; i += arch::PAGE_SIZE) {
      auto window = expect(MapWindow::create(addr, arch::PAGE_SIZE),
                           "failed to create memory window");

      std::memset(window.data(), 0, size);
    }
  }
}
} // namespace yak

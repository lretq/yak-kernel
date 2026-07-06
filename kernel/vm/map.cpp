#include <yak/vm/map.h>

namespace yak {
VmMap kmap = VmMap();

void VmMap::bootstrap_kernel() {
  pm_.bootstrap_kernel();
}

void VmMap::activate() {
  pm_.activate();
}
} // namespace yak

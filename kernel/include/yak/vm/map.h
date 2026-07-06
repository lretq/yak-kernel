#pragma once

#include <yak/arch-pagemap.h>

namespace yak {
class VmMap {
public:
  void bootstrap_kernel();
  void activate();

  inline PageMap &page_map() {
    return pm_;
  }
  }

private:
  PageMap pm_;
};

extern VmMap kmap;

} // namespace yak

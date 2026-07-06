#pragma once

#include <atomic>
#include <optional>
#include <yak/vm/address.h>
#include <yak/vm/flags.h>

namespace yak {

template <typename Traits> class GenericPageMap {
  using Pte = typename Traits::Pte;
  using AtomicPte = std::atomic<Pte>;

public:
  void activate();

  std::optional<paddr_t> reverse_lookup(vaddr_t va, size_t level);

  void enter(vaddr_t va, paddr_t pa, VMProt prot, VMCache cache, size_t level);

  void remove();

  void enter_boot(vaddr_t va, paddr_t pa, VMProt prot, VMCache cache,
                  size_t level);

  void enter_boot_large(paddr_t phys_base, vaddr_t virt_base, size_t length,
                        VMProt prot, VMCache cache);

  void bootstrap_kernel();

private:
  paddr_t top_level;

  std::atomic<Pte> *fetch(vaddr_t va, size_t level, bool allocate,
                          bool is_boot);

  constexpr inline size_t get_index(vaddr_t va, size_t level) const {
    return (va >> Traits::LEVEL_SHIFTS[level]) &
           ((1ULL << Traits::LEVEL_BITS[level]) - 1);
  }
};
} // namespace yak

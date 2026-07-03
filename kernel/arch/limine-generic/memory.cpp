#define pr_fmt(fmt) "limine: " fmt

#include <cstddef>
#include <limine-generic/init.h>
#include <limine-generic/request.h>
#include <limine.h>
#include <span>
#include <string.h>
#include <yak/arch-mm.h>
#include <yak/assert.h>
#include <yak/config.h>
#include <yak/kernel-file.h>
#include <yak/log.h>
#include <yak/math.h>
#include <yak/util.h>
#include <yak/vm/address.h>
#include <yak/vm/flags.h>
#include <yak/vm/map.h>
#include <yak/vm/memblock.h>
#include <yak/vm/pmm.h>

extern "C" {

LIMINE_REQ static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

LIMINE_REQ static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 3,
    .response = nullptr,
};

LIMINE_REQ static volatile struct limine_paging_mode_request
    paging_mode_request = {
        .id = LIMINE_PAGING_MODE_REQUEST_ID,
        .revision = 1,
        .response = nullptr,
#ifdef x86_64
        .mode = LIMINE_PAGING_MODE_X86_64_5LVL,
        .max_mode = LIMINE_PAGING_MODE_X86_64_5LVL,
        .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
#elif defined(riscv64)
        .mode = LIMINE_PAGING_MODE_RISCV_SV57,
        .max_mode = LIMINE_PAGING_MODE_RISCV_SV57,
        .min_mode = LIMINE_PAGING_MODE_RISCV_SV39,
#endif
};

LIMINE_REQ static volatile struct limine_executable_address_request
    executable_address_request = {
        .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
};
}

namespace yak::arch {
vaddr_t HHDM_BASE;
size_t PMAP_LEVELS;

#if defined(riscv64)
vaddr_t PFNDB_BASE;
#endif

[[gnu::weak]]
void post_memblock() {}

[[gnu::weak]]
void post_pmm() {}

[[gnu::weak]]
void early_init() {}

[[gnu::weak]]
void mem_init() {
  limine::mem_init();
}

[[gnu::weak]]
void boot_finalize() {
  limine::mem_reclaim();
}

} // namespace yak::arch

namespace yak::limine {

struct KernelSection {
  const char *name = nullptr;
  vaddr_t start = 0;
  vaddr_t end = 0;
  VMProt prot = 0;
};

static const KernelSection kernel_sections[] = {
    {"limine", reinterpret_cast<vaddr_t>(&__kernel_limine_start),
     reinterpret_cast<vaddr_t>(&__kernel_limine_end), PROT_READ},

    {"text", reinterpret_cast<vaddr_t>(&__kernel_text_start),
     reinterpret_cast<vaddr_t>(&__kernel_text_end), PROT_READ | PROT_EXECUTE},

    {"rodata", reinterpret_cast<vaddr_t>(&__kernel_rodata_start),
     reinterpret_cast<vaddr_t>(&__kernel_rodata_end), PROT_READ},

    {"data", reinterpret_cast<vaddr_t>(&__kernel_data_start),
     reinterpret_cast<vaddr_t>(&__kernel_data_end), PROT_READ | PROT_WRITE},

    {"stack", reinterpret_cast<vaddr_t>(&__kernel_entry_stack_bottom),
     reinterpret_cast<vaddr_t>(&__kernel_entry_stack_top),
     PROT_READ | PROT_WRITE},
};

static void map_kernel() {
  auto res = ensure_request(executable_address_request,
                            "limine missing executable addresses");

  vaddr_t kernel_vbase = res.virtual_base;
  paddr_t kernel_pbase = res.physical_base;

  // Map all sections
  for (const auto &sec : kernel_sections) {
    vaddr_t start = align_down<arch::PAGE_SIZE>(sec.start);
    vaddr_t end = align_up<arch::PAGE_SIZE>(sec.end);

    pr_debug("remap kernel section %s: %#016lx - %#016lx (%#x)\n", sec.name,
             start, end, sec.prot);

    kmap.page_map().enter_boot_large(
        start - kernel_vbase + kernel_pbase, // physical address offset
        start,                               // virtual address
        end - start,                         // size
        sec.prot, CACHE_DEFAULT);
  }

  pr_info("mapped kernel\n");
}

constexpr size_t PFNDB_MIN_PAGE_SIZE = 1;

// returns the end address
template <typename Traits>
static vaddr_t map_pfndb_region(vaddr_t virt_base, size_t length, int nid) {
  assert(is_aligned_pow2<arch::PAGE_SIZE>(virt_base));
  assert(is_aligned_pow2<arch::PAGE_SIZE>(length));

  vaddr_t addr = virt_base;
  vaddr_t end = virt_base + length;

  auto select_level = [](vaddr_t addr, vaddr_t end) {
    for (size_t i = Traits::max_page_size_idx() + 1;
         i-- > PFNDB_MIN_PAGE_SIZE;) {
      const size_t page_size = Traits::PAGE_SIZES[i];
      if (is_aligned_pow2(addr, page_size) && addr + page_size <= end)
        return i;
    }
    return PFNDB_MIN_PAGE_SIZE;
  };

  while (addr < end) {
    size_t level = select_level(addr, end);
    size_t page_size = Traits::PAGE_SIZES[level];

    vaddr_t batch_end = end;
    if (level + 1 < std::size(Traits::PAGE_SIZES)) {
      size_t next_page_size = Traits::PAGE_SIZES[level + 1];
      vaddr_t next_align = align_up(addr + 1, next_page_size);
      batch_end = std::min(next_align, end);
    }

    assert(is_aligned_pow2(addr, page_size));
    assert(is_aligned_pow2(batch_end, page_size) || batch_end == end);

    while (addr < batch_end) {
      // allocate backing on the same NUMA node
      paddr_t pa = expect(boot_memblock.allocate(page_size, page_size, nid),
                          "could not allocate backing pfndb memory");

      kmap.page_map().enter_boot(addr, pa, PROT_READ | PROT_WRITE,
                                 CACHE_DEFAULT, level);

      addr += page_size;
    }
  }

  return end;
}

// Map the virtually contigoouns pfndb
template <typename Traits> static void map_pfndb() {
  constexpr size_t min_page_size = Traits::PAGE_SIZES[PFNDB_MIN_PAGE_SIZE];

  vaddr_t last_mapped_end = arch::PFNDB_BASE;

  for (auto &entry : boot_memblock.memory.view()) {
    pr_debug("physical memory: %#016lx - %#016lx node id %d\n", entry.base_,
             entry.end(), entry.node_id_);

    size_t start_pfn = entry.base_ >> arch::PAGE_SHIFT;
    size_t end_pfn = entry.end() >> arch::PAGE_SHIFT;

    vaddr_t region_start =
        align_down<min_page_size>(arch::PFNDB_BASE + start_pfn * sizeof(Page));
    vaddr_t region_end =
        align_up<min_page_size>(arch::PFNDB_BASE + end_pfn * sizeof(Page));

    // Only back the unmapped portion
    vaddr_t map_start = std::max(region_start, last_mapped_end);
    if (map_start >= region_end)
      continue;

    size_t map_size = region_end - map_start;

    last_mapped_end =
        map_pfndb_region<Traits>(map_start, map_size, entry.node_id_);
  }

  pr_info("mapped pfndb\n");
}

static void init_hhdm() {
  arch::HHDM_BASE = ensure_request(hhdm_request, "limine missing hhdm").offset;
  pr_info("hhdm at 0x%lx\n", arch::HHDM_BASE);
}

static void init_paging_mode() {
  auto paging_mode_res =
      ensure_request(paging_mode_request, "limine missing paging mode");

  switch (paging_mode_res.mode) {
#if defined(x86_64)
  case LIMINE_PAGING_MODE_X86_64_5LVL:
    arch::PMAP_LEVELS = 5;
    break;
  case LIMINE_PAGING_MODE_X86_64_4LVL:
    arch::PMAP_LEVELS = 4;
    break;
#elif defined(riscv64)
  case LIMINE_PAGING_MODE_RISCV_SV57:
    arch::PMAP_LEVELS = 5;
    break;
  case LIMINE_PAGING_MODE_RISCV_SV48:
    arch::PMAP_LEVELS = 4;
    break;
  case LIMINE_PAGING_MODE_RISCV_SV39:
    arch::PMAP_LEVELS = 3;
    break;
#else
#error "Port limine-generic to your platform"
#endif
  }

  pr_info("%zu pmap levels\n", arch::PMAP_LEVELS);
}

static void init_memblock(std::span<limine_memmap_entry *> memmap) {
  for (auto entry : memmap) {
    switch (entry->type) {
    case LIMINE_MEMMAP_USABLE:
      boot_memblock.memory.add(entry->base, entry->length, 0);
      boot_memblock.usable.add(entry->base, entry->length, 0);
      break;

    case LIMINE_MEMMAP_RESERVED:
    case LIMINE_MEMMAP_RESERVED_MAPPED:
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
    case LIMINE_MEMMAP_ACPI_NVS:
    case LIMINE_MEMMAP_BAD_MEMORY:
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
      boot_memblock.memory.add(entry->base, entry->length, 0);
      boot_memblock.reserved.add(entry->base, entry->length, 0);
      break;

    case LIMINE_MEMMAP_FRAMEBUFFER:
    default:
      boot_memblock.reserved.add(entry->base, entry->length, 0);
      break;
    }
  }
}

// clang-format off
[[maybe_unused]]
static const char* memmap_type_to_string(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:                 return "Usable";
        case LIMINE_MEMMAP_RESERVED:               return "Reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI Reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:             return "Bad Memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaimable";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "Executable and Modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
        case LIMINE_MEMMAP_RESERVED_MAPPED:        return "Reserved (Mapped)";
        default:                                   return "Unknown";
    }
}
// clang-format on

constexpr size_t FOUR_GIB = 4ULL * 1024 * 1024 * 1024;

// This *could* use the memblock,
// however I'm not quite sure if that's the best option here
static void map_hhdm(std::span<limine_memmap_entry *> memmap) {
  paddr_t last_end = 0;

  // Then map any other regions from memmap
  for (auto entry : memmap) {
    if (entry->type == LIMINE_MEMMAP_BAD_MEMORY)
      continue;

    paddr_t base = align_down<arch::PAGE_SIZE>(entry->base);
    size_t length = align_up<arch::PAGE_SIZE>(entry->length);

    paddr_t end = base + length;

    if (base < last_end) {
      // fully overlapping
      if (end <= last_end)
        continue;

      // partially overlapping
      base = last_end;
      length = end - base;
    }

    last_end = end;

    VMCache cache = CACHE_DEFAULT;
#ifdef x86_64
    if (entry->type == LIMINE_MEMMAP_FRAMEBUFFER)
      cache = arch::CACHE_WRITECOMBINE;
#endif

    pr_debug("hhdm map: %lx+%lx (type: %s)\n", base, length,
             memmap_type_to_string(entry->type));

    kmap.page_map().enter_boot_large(base, base + arch::HHDM_BASE, length,
                                     PROT_READ | PROT_WRITE, cache);
  }

  pr_info("mapped hhdm\n");
}

auto get_memmap() {
  auto memmap_response =
      ensure_request(memmap_request, "limine missing memmap");

  return std::span(memmap_response.entries, memmap_response.entry_count);
}

void mem_init() {
  init_hhdm();
  init_paging_mode();

#ifdef riscv64
  arch::PFNDB_BASE = 0xffffc00000000000;

  if (arch::PMAP_LEVELS == 3) {
    arch::PFNDB_BASE = arch::HHDM_BASE + GiB(128);
  }
#endif

  auto memmap = get_memmap();

  init_memblock(memmap);

  arch::post_memblock();
  boot_memblock.coalesce_blocks();

  kmap.bootstrap_kernel();

  map_pfndb<GenericPageMapTraits>();
  map_hhdm(memmap);
  map_kernel();

  kmap.activate();
}

void mem_reclaim() {
  auto memmap = get_memmap();
  boot_memblock.coalesce_blocks();
#if CONFIG_DEBUG
  boot_memblock.reserved.print();
#endif

  size_t reclaimed_count = 0;

  for (auto entry : memmap) {
    // search for bootloader memory
    if (entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
      continue;

    boot_memblock.free(entry->base, entry->length);
    reclaimed_count += entry->length;
  }

  pr_info("reclaim %ld KiB\n", reclaimed_count / 1024);
}

} // namespace yak::limine

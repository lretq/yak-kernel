#define pr_fmt(fmt) "init: x86: " fmt

#include <cstdint>
#include <limine-generic/init.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include <x86_64/asm.h>
#include <x86_64/control_regs.h>
#include <x86_64/cpu_features.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <x86_64/lapic.h>
#include <x86_64/msr.h>
#include <x86_64/tss.h>
#include <yak/arch-mm.h>
#include <yak/cpudata.h>
#include <yak/init.h>
#include <yak/log.h>
#include <yak/numa.h>
#include <yak/panic.h>
#include <yak/util.h>
#include <yak/vm/direct.h>
#include <yak/vm/memblock.h>
#include <yak/vm/pmm.h>

namespace yak::arch {

CpuFeatures bsp_cpu_features;

extern void syscall_entry(void);

static void setup_syscalls() {
  uint64_t efer = asm_rdmsr(msr::EFER);
  efer |= efer::SCE;
  asm_wrmsr(msr::EFER, efer);

  asm_wrmsr(msr::LSTAR, (uintptr_t) syscall_entry);

  uint64_t star = asm_rdmsr(msr::STAR);
  star |= ((uint64_t) GDT_SEL_USER_SYSCALL << 48);
  star |= ((uint64_t) GDT_SEL_KERNEL_CODE << 32);
  asm_wrmsr(msr::STAR, star);
}

namespace pat {
enum class Type : uint8_t {
  UC = 0x0,
  WC = 0x1,
  WT = 0x4,
  WP = 0x5,
  WB = 0x6,
  UC_MINUS = 0x7,
};

constexpr uint64_t entry(Type t, int index) {
  return static_cast<uint64_t>(t) << (index * 8);
}
} // namespace pat

// clang-format off
static void pat_init() {
  if (!bsp_cpu_features.pat) panic("PAT unsupported.");

  using namespace pat;

  constexpr uint64_t pat =
          entry(Type::WB, 0)
        | entry(Type::WT, 1)
        | entry(Type::UC_MINUS, 2)
        | entry(Type::UC, 3)
        | entry(Type::WP, 4)
        | entry(Type::WC, 5);

    asm_wrmsr(msr::PAT, pat);
}
// clang-format on

uint32_t cpu_get_apic_id(void) {
  uint32_t eax, ebx, ecx, edx;

  if (bsp_cpu_features.max_leaf >= 0x1f) {
    asm_cpuid(0x1f, 0, &eax, &ebx, &ecx, &edx);
    if (ebx != 0)
      return edx;
  }

  if (bsp_cpu_features.max_leaf >= 0x0b) {
    asm_cpuid(0x0b, 0, &eax, &ebx, &ecx, &edx);
    if (ebx != 0)
      return edx;
  }

  asm_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
  return ebx >> 24;
}

static void setup_cpu() {
  idt_reload();

  // initialize & load GDT (percpu!)
  gdt_init();
  gdt_reload();

  asm_wrcr8(0);

  setup_syscalls();

  pat_init();

  uint64_t cr0 = asm_rdcr0();
  cr0 &= ~(cr0::CD | cr0::NW);
  cr0 |= cr0::WP;
  asm_wrcr0(cr0);

  uint64_t cr4 = asm_rdcr4();
  if (bsp_cpu_features.pge)
    cr4 |= cr4::PGE;
  if (bsp_cpu_features.pse)
    cr4 |= cr4::PSE;
  asm_wrcr4(cr4);

  if (bsp_cpu_features.nx) {
    uint64_t efer = asm_rdmsr(msr::EFER);
    efer |= efer::NXE;
    asm_wrmsr(msr::EFER, efer);
  }

  auto apic_id = cpu_get_apic_id();
  CPUDATA_STORE(md.apic_id, apic_id);
}

static void detect_features(CpuFeatures &f) {
  uint32_t eax, ebx, ecx, edx;

  asm_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
  f.max_leaf = eax;

  asm_cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
  f.nx = edx & (1 << 20);
  f.pdpe1gb = edx & (1 << 26);
  asm_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
  f.pge = edx & (1 << 13);
  f.pat = edx & (1 << 16);
  f.pcid = ecx & (1 << 17);
  f.pse = edx & (1 << 3);
  f.xsave = ecx & (1 << 26);
  f.avx = ecx & (1 << 28);
  f.x2apic = ecx & (1 << 21);
  f.mwait = ecx & (1 << 3);
}

void early_init() {
  // Setup GSBASE so that %gs:0 == the kernel ELF's PERCPU area
  //
  // kernel_entry() makes sure that cpudata->self already points to self
  // (as well as initializing all other core kernel fields)
  //
  // no need to zero memory for the BSP. percpu is bss!
  asm_wrmsr(msr::GSBASE, (uint64_t) __percpu_start);

  detect_features(bsp_cpu_features);

  idt_init();

  setup_cpu();
}

void post_pmm() {
  tss_cpu_init();
}

static uacpi_iteration_decision check_srat([[maybe_unused]] uacpi_handle user,
                                           acpi_entry_hdr *hdr) {
  switch (hdr->type) {
  case ACPI_SRAT_ENTRY_TYPE_PROCESSOR_AFFINITY: {
    auto ps = reinterpret_cast<acpi_srat_processor_affinity *>(hdr);
    if (!(ps->flags & ACPI_SRAT_PROCESSOR_ENABLED))
      break;

    uint32_t proximity_domain = ps->proximity_domain_low;
    proximity_domain |= ps->proximity_domain_high[0] << 8;
    proximity_domain |= ps->proximity_domain_high[1] << 16;
    proximity_domain |= ps->proximity_domain_high[2] << 24;
    pr_debug("APIC affinity: id=%u clock_domain=%u domain=%u\n", ps->id,
             ps->clock_domain, proximity_domain);

    auto &dom = Domain::from_firmware_id(proximity_domain);

    if (ps->id == CPUDATA_LOAD(md.apic_id))
      CPUDATA_STORE(numa_domain, dom.id);

    break;
  }
  case ACPI_SRAT_ENTRY_TYPE_X2APIC_AFFINITY: {
    auto ps = reinterpret_cast<acpi_srat_x2apic_affinity *>(hdr);
    if (!(ps->flags & ACPI_SRAT_PROCESSOR_ENABLED))
      break;

    uint32_t proximity_domain = ps->proximity_domain;
    pr_debug("x2APIC affinity: id=%u clock_domain=%u domain=%u\n", ps->id,
             ps->clock_domain, proximity_domain);

    auto &dom = Domain::from_firmware_id(ps->proximity_domain);

    if (ps->id == CPUDATA_LOAD(md.apic_id))
      CPUDATA_STORE(numa_domain, dom.id);

    break;
  }
  case ACPI_SRAT_ENTRY_TYPE_MEMORY_AFFINITY: {
    auto ms = reinterpret_cast<acpi_srat_memory_affinity *>(hdr);
    if (!(ms->flags & ACPI_SRAT_PROCESSOR_ENABLED))
      break;

    pr_debug("memory affinity: %#016lx - %#016lx (length: %lx) => domain %d\n",
             ms->address, ms->address + ms->length, ms->length,
             ms->proximity_domain);

    auto logical_id = Domain::from_firmware_id(ms->proximity_domain).id;

    boot_memblock.assign_node_to_range(ms->address, ms->length, logical_id);

    auto &aff = pmm_affinities.alloc_slot();
    aff.domain = logical_id;
    aff.base = ms->address;
    aff.length = ms->length;
    break;
  }

  default:
    break;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

void parse_numa() {
  uacpi_table tbl;
  if (uacpi_table_find_by_signature("SRAT", &tbl) != UACPI_STATUS_OK)
    return;

  uacpi_for_each_subtable(tbl.hdr, sizeof(acpi_srat), check_srat, nullptr);

  uacpi_table_unref(&tbl);
}

static std::optional<MapWindow> early_buffer_window;

void post_memblock() {
  auto buf = expect(boot_memblock.allocate(PAGE_SIZE, PAGE_SIZE, NUMA_ANY),
                    "could not allocate early table buffer");
  pr_debug("early table buffer: %#lx\n", buf);

  auto total_before =
      boot_memblock.usable.total_size_ + boot_memblock.reserved.total_size_;

  auto win = expect(MapWindow::create(buf, PAGE_SIZE),
                    "failed to map uacpi early buffer");
  early_buffer_window.emplace(std::move(win));

  uacpi_setup_early_table_access(win.data(), PAGE_SIZE);

  parse_numa();

  auto total_after =
      boot_memblock.usable.total_size_ + boot_memblock.reserved.total_size_;

  // try to catch any memblock insertion bugs
  assert(total_before == total_after);
}

} // namespace yak::arch

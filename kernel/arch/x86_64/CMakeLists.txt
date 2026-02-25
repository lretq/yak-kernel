yak_add_sources(
	src/pmap.c
	src/ipl.c
	src/archctl.c
	src/hpet.c
	src/kvmclock.c
	src/sched.c
	src/pci.c
	src/gdt.c
	src/isr.c
	src/apic.c
	src/init.c
	src/fpu.c

	src/PlatformExpert.cc

	src/idt.S
	src/sched.S
	src/start.S
	src/string.S
	src/syscall.S
)

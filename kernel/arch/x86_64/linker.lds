/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)

/* We want the symbol kmain to be our entry point */
ENTRY(_start)

/* Define the program headers we want so the bootloader gives us the right */
/* MMU permissions; this also allows us to exert more control over the linking */
/* process. */
PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    /* We want to be placed in the topmost 2GiB of the address space, for optimisations */
    /* and because that is what the Limine spec mandates. */
    /* Any address in this region will do, but often 0xffffffff80000000 is chosen as */
    /* that is the beginning of the region. */
    . = 0xffffffff80000000;

    /* Define a section to contain the Limine requests and assign it to its own PHDR */
    PROVIDE_HIDDEN(__kernel_limine_start = .);

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    PROVIDE_HIDDEN(__kernel_limine_end = .);

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    PROVIDE_HIDDEN(__kernel_text_start = .);

    .text : {
        *(.text .text.*)
    } :text

    PROVIDE_HIDDEN(__kernel_text_end = .);

    /* Move to the next memory page for .rodata */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    PROVIDE_HIDDEN(__kernel_rodata_start = .);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .init_array : {
        __init_array = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    	KEEP(*(.init_array .ctors))
        __init_array_end = .;
    } :rodata

    PROVIDE_HIDDEN(__kernel_rodata_end = .);

    /* Move to the next memory page for .data */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    PROVIDE_HIDDEN(__kernel_data_start = .);

    .data : {
        *(.data .data.*)
    } :data

    .init_node ALIGN(8) : {
    	PROVIDE_HIDDEN(__kernel_init_node_start = .);
    	KEEP(*(.init_node .init_node.*))
    	PROVIDE_HIDDEN(__kernel_init_node_end = .);
    } :data

    .init_stage ALIGN(8) : {
    	PROVIDE_HIDDEN(__kernel_init_stage_start = .);
    	KEEP(*(.init_stage .init_stage.*))
	PROVIDE_HIDDEN(__kernel_init_stage_end = .);
    } :data

    /* store percpu variables on a seperate page */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .percpu : {
	PROVIDE_HIDDEN(__kernel_percpu_start = .);
	KEEP(*(.percpu.cpudata))
	KEEP(*(.percpu .percpu.*))
	PROVIDE_HIDDEN(__kernel_percpu_end = .);
    } :data

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    /* NOTE: .bss needs to be the last thing mapped to :data, otherwise lots of */
    /* unnecessary zeros will be written to the binary. */
    /* If you need, for example, .init_array and .fini_array, those should be placed */
    /* above this. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    PROVIDE_HIDDEN(__kernel_data_end = .);

    /* Discard .note.* and .eh_frame* since they may cause issues on some hosts. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}

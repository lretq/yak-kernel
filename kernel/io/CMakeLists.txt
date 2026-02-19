yak_add_sources(
    init.cc
    Device.cc
    Registry.cc
    WorkLoop.cc
    TreeNode.cc
    Personality.cc

    tty/tty.c
    tty/n_tty.c
    tty/console.c
)

add_subdirectory(acpi)
add_subdirectory(pci)

add_subdirectory(drivers)

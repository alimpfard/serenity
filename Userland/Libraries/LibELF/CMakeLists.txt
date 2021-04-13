serenity_install_sources("Userland/Libraries/LibELF")

set(SOURCES
        dummy.cpp)
serenity_lib(LibELF elf)
target_link_libraries(LibELF LibC)

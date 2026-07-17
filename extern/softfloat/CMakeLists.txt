# POM68K — vendored 80-bit softfloat (see VENDOR.md for provenance)
#
# Kept as a separate static C library: extern/moira (MIT) only *links*
# against it; the softfloat sources are GPLv2+/SoftFloat-2a (see VENDOR.md).

add_library(softfloat68k STATIC
    softfloat.c
    softfloat_decimal.c
    softfloat_fpsp.c
)
target_include_directories(softfloat68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(softfloat68k PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)
endif()

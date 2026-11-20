add_library(moira STATIC

Moira.cpp
MoiraCore68000.cpp
MoiraCore68010.cpp
MoiraCore68020.cpp
MoiraDebugger.cpp
)

# The FPU (68881/68882, 68040) computes in SoftFloat-2a (GPLv2+, kept in its
# own library; Moira only links against it). Its exported symbols carry the
# prefix of softfloat-prefix.h, so it links next to Musashi's own copy.
# A host that already defines the target `softfloat68k` keeps its own;
# otherwise MOIRA_SOFTFLOAT_DIR names the directory to build it from
# (default: `softfloat/` beside this directory).
if(NOT TARGET softfloat68k)
    set(MOIRA_SOFTFLOAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../softfloat"
        CACHE PATH "Directory of the SoftFloat library Moira's FPU links")
    if(NOT EXISTS "${MOIRA_SOFTFLOAT_DIR}/softfloat.c")
        message(FATAL_ERROR "Moira: SoftFloat not found in ${MOIRA_SOFTFLOAT_DIR}; "
            "set MOIRA_SOFTFLOAT_DIR")
    endif()
    enable_language(C)
    add_subdirectory(${MOIRA_SOFTFLOAT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/softfloat)
endif()
target_link_libraries(moira PUBLIC softfloat68k)

if (NOT MSVC)
    target_compile_options(moira PUBLIC -Wno-unused-parameter)
    target_compile_options(moira PUBLIC -Wno-unused-but-set-parameter)
    target_compile_options(moira PUBLIC -Wno-unused-but-set-variable)
    target_compile_options(moira PUBLIC -Wno-missing-field-initializers)
endif()

if(MSVC)
    target_compile_options(moira PUBLIC /W4 /bigobj) # /WX disabled for now
    target_compile_options(moira PUBLIC /wd4100 /wd4201 /wd4324 /wd4458 /wd4127)
endif()

if(MINGW)
    target_compile_options(moira PUBLIC -Wa,-mbig-obj)
endif()

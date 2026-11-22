cmake_minimum_required(VERSION 3.16)
project(sst_dirk CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dirk's DEFAULT behaviour with the fault machinery the MMU vectors need.
add_compile_definitions(
    MOIRA_EMULATE_ADDRESS_ERROR=true
    MOIRA_EMULATE_BUS_ERROR=true
    MOIRA_PRECISE_TIMING=true)

set(DIRK_MOIRA "" CACHE PATH "Moira/ directory of Dirk's FPU branch")
set(POM68K_TESTS "" CACHE PATH "POM68K tests/ directory (SstKnown.h)")
add_subdirectory(${DIRK_MOIRA} moira)
add_executable(sst_dirk sst_dirk.cpp)
target_include_directories(sst_dirk PRIVATE ${DIRK_MOIRA} ${POM68K_TESTS})
target_link_libraries(sst_dirk PRIVATE moira)

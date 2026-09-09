# SCC asynchronous serial wire through the two real POSIX host endpoints.
# PTYs have no Windows equivalent; TCP remains available there in a future
# WinSock backend, so the union records this gate as host-conditional.
if(NOT WIN32 AND NOT EMSCRIPTEN)
add_executable(scc_serial_host_test tests/scc_serial_host_test.cpp)
target_link_libraries(scc_serial_host_test PRIVATE pom68k_core)
add_test(NAME scc_serial_host_test COMMAND scc_serial_host_test)
endif()

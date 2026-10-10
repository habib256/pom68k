# SCC asynchronous serial wire through the host endpoints and the « Ports
# série » terminal. The TCP half and the terminal run everywhere (BSD
# sockets, Winsock on Windows); the PTY half is Unix-only and the Windows
# build checks that PTY is refused.
if(NOT EMSCRIPTEN)
add_executable(scc_serial_host_test tests/scc_serial_host_test.cpp)
target_link_libraries(scc_serial_host_test PRIVATE pom68k_core)
add_test(NAME scc_serial_host_test COMMAND scc_serial_host_test)
endif()

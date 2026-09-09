# LLAP wire gates: two SCC peers, the in-process node's prompt address
# defence, and the Mini vMac / TashRouter multicast transport.
add_executable(llap_loop_test tests/llap_loop_test.cpp)
target_link_libraries(llap_loop_test PRIVATE pom68k_core)
add_test(NAME llap_loop_test COMMAND llap_loop_test)

add_executable(llap_address_defense_test tests/llap_address_defense_test.cpp)
target_link_libraries(llap_address_defense_test PRIVATE pom68k_core)
add_test(NAME llap_address_defense_test COMMAND llap_address_defense_test)

add_executable(ltoudp_test tests/ltoudp_test.cpp)
target_link_libraries(ltoudp_test PRIVATE pom68k_core)
add_test(NAME ltoudp_test COMMAND ltoudp_test)

# A save state is (RAM, disk content): DiskTimeline.h. Write-back off and on,
# same and fresh process (the `.pomundo` reverse journal rewinds the file),
# alternating states, changed base, deleted/torn/compacted/uncreatable
# journal, topology refusals and ATA. Scratch images; no ROM, no asset.
add_executable(media_timeline_test tests/media_timeline_test.cpp)
target_link_libraries(media_timeline_test PRIVATE pom68k_core)
add_test(NAME media_timeline_test COMMAND media_timeline_test)

# ROM store gates — the catalogue, the zip reader, the downloader and the
# importer (src/RomCatalog.h, ZipReader, RomStore, RomFetch, RomImport,
# HostProcess). All asset-free: they read the source tree, synthetic dumps
# and a fake server; nothing is downloaded and no Apple byte is needed.

add_executable(zip_reader_test tests/zip_reader_test.cpp)
target_link_libraries(zip_reader_test PRIVATE pom68k_roms)
add_test(NAME zip_reader_test COMMAND zip_reader_test)

# Catalogue ↔ assets.lock ↔ firmware::kManifest ↔ kMachineProfiles ↔ the
# paths the runtime opens; SHA-256/CRC-32 vectors; the RetroBIOS pin.
add_executable(rom_catalog_test tests/rom_catalog_test.cpp)
target_link_libraries(rom_catalog_test PRIVATE pom68k_roms)
add_test(NAME rom_catalog_test
         COMMAND rom_catalog_test ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(rom_fetch_test tests/rom_fetch_test.cpp)
target_link_libraries(rom_fetch_test PRIVATE pom68k_roms)
add_test(NAME rom_fetch_test COMMAND rom_fetch_test)

add_executable(rom_import_test tests/rom_import_test.cpp)
target_link_libraries(rom_import_test PRIVATE pom68k_roms)
add_test(NAME rom_import_test COMMAND rom_import_test)

# The archive tooling's offline self-tests (no network, no roms/ needed).
add_test(NAME asset_archives_script_test
         COMMAND python3 tools/build_asset_archives.py --self-test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fetch_test_media_test
         COMMAND python3 tools/fetch_test_media.py --self-test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

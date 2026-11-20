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

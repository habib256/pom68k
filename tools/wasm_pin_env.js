// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// --pre-js of the WASM pin replay programs (cmake/Pom68kWasmPins.cmake).
// An Emscripten program's getenv() reads Emscripten's own ENV object, not
// node's process.env, so a gate's POM68K_* selectors (the model of a shared
// binary, POM68K_TEST_AGENT, POM68K_PIN_PPM) would otherwise vanish
// between ctest and main(). Copied before main, never written back.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
    for (var k in process.env) ENV[k] = process.env[k];
});

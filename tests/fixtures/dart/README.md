# Synthetic DART LZH vectors

These three 20,960-byte blocks contain only generated test data, no ROMs or
system software. `zero` checks DART's zero-filled dictionary; `pattern`
checks sector/tag layout, overlapping matches and window wrap; `literal`
checks pseudorandom data and adaptive-tree updates near the chunk limit.
Expected bytes are defined in `tools/generate_dart_vectors.rs` and checked
independently in `dart_image_test.cpp`.

The encoded bytes were generated with retrocompressor 1.0.1, commit
`91ca4c3a3411059cf2608fc8def20651a9ff6b88`, using Snow's DART parameters.
Regeneration, from a checkout of that reference codec:

```sh
cp /path/to/pom68k/tools/generate_dart_vectors.rs examples/pom_dart_vectors.rs
cargo run --release --example pom_dart_vectors -- /path/to/pom68k/tests/fixtures/dart
```

The generator and codec are unnecessary when building or running the tests.

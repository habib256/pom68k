# Audio cross-section: the guest output stage and the GUI host-clock boundary.
# Both gates are deterministic and asset-free; no sound device is required.

# Original LC-family DFAC three-wire shift/latch and attenuation stage.
add_executable(dfac_test tests/dfac_test.cpp)
target_link_libraries(dfac_test PRIVATE pom68k_core)
add_test(NAME dfac_test COMMAND dfac_test)

# Rational conversion from guest crystal to native host callback clock,
# including irregular GUI-frame chunks and a ten-minute tempo proof.
add_executable(audio_resampler_test tests/audio_resampler_test.cpp)
add_test(NAME audio_resampler_test COMMAND audio_resampler_test)

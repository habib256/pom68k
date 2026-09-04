# Product-only gate inventory and its manifest classification.

set(POM68K_PRODUCT_LLE_ONLY_GATES "")
if(POM68K_PRODUCT_LLE_GATES)
    set(POM68K_PRODUCT_LLE_ONLY_GATES
        lle_a64_q605_preflight
        lle_a64_centris_preflight
        lle_a64_q700_preflight
        lle_a64_q900_preflight
        lle_a64_q630_preflight
        lle_a64_forced_hle_refused
        lle_a64_missing_firmware_refused
        lle_a64_q900_forced_hle_refused)
endif()

function(pom68k_set_gate_configuration test out)
    set(config all)
    if(test IN_LIST POM68K_PRODUCT_LLE_ONLY_GATES)
        set(config product-lle)
    endif()
    set_property(TEST ${test} PROPERTY POM68K_CONFIG ${config})
    set(${out} ${config} PARENT_SCOPE)
endfunction()

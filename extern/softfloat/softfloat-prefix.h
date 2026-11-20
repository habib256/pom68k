/* POM68K: every external symbol of this library, renamed.
 *
 * SoftFloat-2a's function names (floatx80_add, int32_to_floatx80, ...) are
 * the same in every emulator that vendors it. A program that links this
 * library next to another copy - upstream Moira's test runner links
 * Musashi, which bundles its own - fails on duplicate definitions, or
 * worse, resolves one emulator's calls to the other's variant. The prefix
 * makes this copy private to the core that uses it; no source line of the
 * arithmetic changes.
 *
 * Included first by softfloat.h, so the library and its users agree.
 * softfloat_symbols_test (POM68K, cmake/Pom68kCpuGates.cmake) fails when
 * the library exports a symbol this list does not rename.
 */
#ifndef SOFTFLOAT_PREFIX_H
#define SOFTFLOAT_PREFIX_H

#define extractFloatx80Exp                pom68k_sf_extractFloatx80Exp
#define extractFloatx80Frac               pom68k_sf_extractFloatx80Frac
#define extractFloatx80Sign               pom68k_sf_extractFloatx80Sign
#define float32_to_floatx80               pom68k_sf_float32_to_floatx80
#define float32_to_floatx80_allowunnormal pom68k_sf_float32_to_floatx80_allowunnormal
#define float64_squash_input_denormal     pom68k_sf_float64_squash_input_denormal
#define float64_to_floatx80               pom68k_sf_float64_to_floatx80
#define float64_to_floatx80_allowunnormal pom68k_sf_float64_to_floatx80_allowunnormal
#define floatdecimal_to_floatx80          pom68k_sf_floatdecimal_to_floatx80
#define floatx80_abs                      pom68k_sf_floatx80_abs
#define floatx80_acos                     pom68k_sf_floatx80_acos
#define floatx80_add                      pom68k_sf_floatx80_add
#define floatx80_asin                     pom68k_sf_floatx80_asin
#define floatx80_atan                     pom68k_sf_floatx80_atan
#define floatx80_atanh                    pom68k_sf_floatx80_atanh
#define floatx80_cmp                      pom68k_sf_floatx80_cmp
#define floatx80_cos                      pom68k_sf_floatx80_cos
#define floatx80_cosh                     pom68k_sf_floatx80_cosh
#define floatx80_denormalize              pom68k_sf_floatx80_denormalize
#define floatx80_div                      pom68k_sf_floatx80_div
#define floatx80_eq                       pom68k_sf_floatx80_eq
#define floatx80_etox                     pom68k_sf_floatx80_etox
#define floatx80_etoxm1                   pom68k_sf_floatx80_etoxm1
#define floatx80_getexp                   pom68k_sf_floatx80_getexp
#define floatx80_getman                   pom68k_sf_floatx80_getman
#define floatx80_le                       pom68k_sf_floatx80_le
#define floatx80_log10                    pom68k_sf_floatx80_log10
#define floatx80_log2                     pom68k_sf_floatx80_log2
#define floatx80_logn                     pom68k_sf_floatx80_logn
#define floatx80_lognp1                   pom68k_sf_floatx80_lognp1
#define floatx80_lt                       pom68k_sf_floatx80_lt
#define floatx80_mod                      pom68k_sf_floatx80_mod
#define floatx80_move                     pom68k_sf_floatx80_move
#define floatx80_mul                      pom68k_sf_floatx80_mul
#define floatx80_neg                      pom68k_sf_floatx80_neg
#define floatx80_normalize                pom68k_sf_floatx80_normalize
#define floatx80_rem                      pom68k_sf_floatx80_rem
#define floatx80_round32                  pom68k_sf_floatx80_round32
#define floatx80_round64                  pom68k_sf_floatx80_round64
#define floatx80_round_to_float32         pom68k_sf_floatx80_round_to_float32
#define floatx80_round_to_float64         pom68k_sf_floatx80_round_to_float64
#define floatx80_round_to_int             pom68k_sf_floatx80_round_to_int
#define floatx80_round_to_int_toward_zero pom68k_sf_floatx80_round_to_int_toward_zero
#define floatx80_scale                    pom68k_sf_floatx80_scale
#define floatx80_sgldiv                   pom68k_sf_floatx80_sgldiv
#define floatx80_sglmul                   pom68k_sf_floatx80_sglmul
#define floatx80_sin                      pom68k_sf_floatx80_sin
#define floatx80_sincos                   pom68k_sf_floatx80_sincos
#define floatx80_sinh                     pom68k_sf_floatx80_sinh
#define floatx80_sqrt                     pom68k_sf_floatx80_sqrt
#define floatx80_sub                      pom68k_sf_floatx80_sub
#define floatx80_tan                      pom68k_sf_floatx80_tan
#define floatx80_tanh                     pom68k_sf_floatx80_tanh
#define floatx80_tentox                   pom68k_sf_floatx80_tentox
#define floatx80_to_float32               pom68k_sf_floatx80_to_float32
#define floatx80_to_float64               pom68k_sf_floatx80_to_float64
#define floatx80_to_floatdecimal          pom68k_sf_floatx80_to_floatdecimal
#define floatx80_to_floatx80              pom68k_sf_floatx80_to_floatx80
#define floatx80_to_int16                 pom68k_sf_floatx80_to_int16
#define floatx80_to_int32                 pom68k_sf_floatx80_to_int32
#define floatx80_to_int32_round_to_zero   pom68k_sf_floatx80_to_int32_round_to_zero
#define floatx80_to_int64                 pom68k_sf_floatx80_to_int64
#define floatx80_to_int8                  pom68k_sf_floatx80_to_int8
#define floatx80_tst                      pom68k_sf_floatx80_tst
#define floatx80_twotox                   pom68k_sf_floatx80_twotox
#define getFloatInternalFloatx80          pom68k_sf_getFloatInternalFloatx80
#define getFloatInternalGRS               pom68k_sf_getFloatInternalGRS
#define getFloatInternalOverflow          pom68k_sf_getFloatInternalOverflow
#define getFloatInternalRoundedAll        pom68k_sf_getFloatInternalRoundedAll
#define getFloatInternalRoundedSome       pom68k_sf_getFloatInternalRoundedSome
#define getFloatInternalUnderflow         pom68k_sf_getFloatInternalUnderflow
#define getFloatInternalUnrounded         pom68k_sf_getFloatInternalUnrounded
#define getRoundedFloatInternal           pom68k_sf_getRoundedFloatInternal
#define int32_to_floatx80                 pom68k_sf_int32_to_floatx80
#define int64_to_floatx80                 pom68k_sf_int64_to_floatx80
#define normalizeFloatx80Subnormal        pom68k_sf_normalizeFloatx80Subnormal
#define packFloatx80                      pom68k_sf_packFloatx80
#define roundAndPackFloatx80              pom68k_sf_roundAndPackFloatx80
#define roundSaveFloatx80Internal         pom68k_sf_roundSaveFloatx80Internal
#define roundSigAndPackFloatx80           pom68k_sf_roundSigAndPackFloatx80

#endif

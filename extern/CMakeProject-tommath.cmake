set(TOMMATH_SRC "libtommath/bn_cutoffs.c"
                "libtommath/bn_deprecated.c"
                "libtommath/bn_mp_2expt.c"
                "libtommath/bn_mp_abs.c"
                "libtommath/bn_mp_add.c"
                "libtommath/bn_mp_add_d.c"
                "libtommath/bn_mp_addmod.c"
                "libtommath/bn_mp_and.c"
                "libtommath/bn_mp_clamp.c"
                "libtommath/bn_mp_clear.c"
                "libtommath/bn_mp_clear_multi.c"
                "libtommath/bn_mp_cmp.c"
                "libtommath/bn_mp_cmp_d.c"
                "libtommath/bn_mp_cmp_mag.c"
                "libtommath/bn_mp_cnt_lsb.c"
                "libtommath/bn_mp_complement.c"
                "libtommath/bn_mp_copy.c"
                "libtommath/bn_mp_count_bits.c"
                "libtommath/bn_mp_decr.c"
                "libtommath/bn_mp_div_2.c"
                "libtommath/bn_mp_div_2d.c"
                "libtommath/bn_mp_div_3.c"
                "libtommath/bn_mp_div.c"
                "libtommath/bn_mp_div_d.c"
                "libtommath/bn_mp_dr_is_modulus.c"
                "libtommath/bn_mp_dr_reduce.c"
                "libtommath/bn_mp_dr_setup.c"
                "libtommath/bn_mp_error_to_string.c"
                "libtommath/bn_mp_exch.c"
                "libtommath/bn_mp_exptmod.c"
                "libtommath/bn_mp_expt_u32.c"
                "libtommath/bn_mp_exteuclid.c"
                "libtommath/bn_mp_fread.c"
                "libtommath/bn_mp_from_sbin.c"
                "libtommath/bn_mp_from_ubin.c"
                "libtommath/bn_mp_fwrite.c"
                "libtommath/bn_mp_gcd.c"
                "libtommath/bn_mp_get_double.c"
                "libtommath/bn_mp_get_i32.c"
                "libtommath/bn_mp_get_i64.c"
                "libtommath/bn_mp_get_l.c"
                "libtommath/bn_mp_get_ll.c"
                "libtommath/bn_mp_get_mag_u32.c"
                "libtommath/bn_mp_get_mag_u64.c"
                "libtommath/bn_mp_get_mag_ul.c"
                "libtommath/bn_mp_get_mag_ull.c"
                "libtommath/bn_mp_grow.c"
                "libtommath/bn_mp_incr.c"
                "libtommath/bn_mp_init.c"
                "libtommath/bn_mp_init_copy.c"
                "libtommath/bn_mp_init_i32.c"
                "libtommath/bn_mp_init_i64.c"
                "libtommath/bn_mp_init_l.c"
                "libtommath/bn_mp_init_ll.c"
                "libtommath/bn_mp_init_multi.c"
                "libtommath/bn_mp_init_set.c"
                "libtommath/bn_mp_init_size.c"
                "libtommath/bn_mp_init_u32.c"
                "libtommath/bn_mp_init_u64.c"
                "libtommath/bn_mp_init_ul.c"
                "libtommath/bn_mp_init_ull.c"
                "libtommath/bn_mp_invmod.c"
                "libtommath/bn_mp_iseven.c"
                "libtommath/bn_mp_isodd.c"
                "libtommath/bn_mp_is_square.c"
                "libtommath/bn_mp_kronecker.c"
                "libtommath/bn_mp_lcm.c"
                "libtommath/bn_mp_log_u32.c"
                "libtommath/bn_mp_lshd.c"
                "libtommath/bn_mp_mod_2d.c"
                "libtommath/bn_mp_mod.c"
                "libtommath/bn_mp_mod_d.c"
                "libtommath/bn_mp_montgomery_calc_normalization.c"
                "libtommath/bn_mp_montgomery_reduce.c"
                "libtommath/bn_mp_montgomery_setup.c"
                "libtommath/bn_mp_mul_2.c"
                "libtommath/bn_mp_mul_2d.c"
                "libtommath/bn_mp_mul.c"
                "libtommath/bn_mp_mul_d.c"
                "libtommath/bn_mp_mulmod.c"
                "libtommath/bn_mp_neg.c"
                "libtommath/bn_mp_or.c"
                "libtommath/bn_mp_pack.c"
                "libtommath/bn_mp_pack_count.c"
                "libtommath/bn_mp_prime_fermat.c"
                "libtommath/bn_mp_prime_frobenius_underwood.c"
                "libtommath/bn_mp_prime_is_prime.c"
                "libtommath/bn_mp_prime_miller_rabin.c"
                "libtommath/bn_mp_prime_next_prime.c"
                "libtommath/bn_mp_prime_rabin_miller_trials.c"
                "libtommath/bn_mp_prime_rand.c"
                "libtommath/bn_mp_prime_strong_lucas_selfridge.c"
                "libtommath/bn_mp_radix_size.c"
                "libtommath/bn_mp_radix_smap.c"
                "libtommath/bn_mp_rand.c"
                "libtommath/bn_mp_read_radix.c"
                "libtommath/bn_mp_reduce_2k.c"
                "libtommath/bn_mp_reduce_2k_l.c"
                "libtommath/bn_mp_reduce_2k_setup.c"
                "libtommath/bn_mp_reduce_2k_setup_l.c"
                "libtommath/bn_mp_reduce.c"
                "libtommath/bn_mp_reduce_is_2k.c"
                "libtommath/bn_mp_reduce_is_2k_l.c"
                "libtommath/bn_mp_reduce_setup.c"
                "libtommath/bn_mp_root_u32.c"
                "libtommath/bn_mp_rshd.c"
                "libtommath/bn_mp_sbin_size.c"
                "libtommath/bn_mp_set.c"
                "libtommath/bn_mp_set_double.c"
                "libtommath/bn_mp_set_i32.c"
                "libtommath/bn_mp_set_i64.c"
                "libtommath/bn_mp_set_l.c"
                "libtommath/bn_mp_set_ll.c"
                "libtommath/bn_mp_set_u32.c"
                "libtommath/bn_mp_set_u64.c"
                "libtommath/bn_mp_set_ul.c"
                "libtommath/bn_mp_set_ull.c"
                "libtommath/bn_mp_shrink.c"
                "libtommath/bn_mp_signed_rsh.c"
                "libtommath/bn_mp_sqr.c"
                "libtommath/bn_mp_sqrmod.c"
                "libtommath/bn_mp_sqrt.c"
                "libtommath/bn_mp_sqrtmod_prime.c"
                "libtommath/bn_mp_sub.c"
                "libtommath/bn_mp_sub_d.c"
                "libtommath/bn_mp_submod.c"
                "libtommath/bn_mp_to_radix.c"
                "libtommath/bn_mp_to_sbin.c"
                "libtommath/bn_mp_to_ubin.c"
                "libtommath/bn_mp_ubin_size.c"
                "libtommath/bn_mp_unpack.c"
                "libtommath/bn_mp_xor.c"
                "libtommath/bn_mp_zero.c"
                "libtommath/bn_prime_tab.c"
                "libtommath/bn_s_mp_add.c"
                "libtommath/bn_s_mp_balance_mul.c"
                "libtommath/bn_s_mp_exptmod.c"
                "libtommath/bn_s_mp_exptmod_fast.c"
                "libtommath/bn_s_mp_get_bit.c"
                "libtommath/bn_s_mp_invmod_fast.c"
                "libtommath/bn_s_mp_invmod_slow.c"
                "libtommath/bn_s_mp_karatsuba_mul.c"
                "libtommath/bn_s_mp_karatsuba_sqr.c"
                "libtommath/bn_s_mp_montgomery_reduce_fast.c"
                "libtommath/bn_s_mp_mul_digs.c"
                "libtommath/bn_s_mp_mul_digs_fast.c"
                "libtommath/bn_s_mp_mul_high_digs.c"
                "libtommath/bn_s_mp_mul_high_digs_fast.c"
                "libtommath/bn_s_mp_prime_is_divisible.c"
                "libtommath/bn_s_mp_rand_jenkins.c"
                "libtommath/bn_s_mp_rand_platform.c"
                "libtommath/bn_s_mp_reverse.c"
                "libtommath/bn_s_mp_sqr.c"
                "libtommath/bn_s_mp_sqr_fast.c"
                "libtommath/bn_s_mp_sub.c"
                "libtommath/bn_s_mp_toom_mul.c"
                "libtommath/bn_s_mp_toom_sqr.c")

set(TOMMATH_HPP "libtommath/tommath_class.h"
                "libtommath/tommath_cutoffs.h"
                "libtommath/tommath.h"
                "libtommath/tommath_private.h"
                "libtommath/tommath_superclass.h")


source_group("" FILES ${TOMMATH_SRC} ${TOMMATH_HPP})

add_library("tommath" STATIC ${TOMMATH_SRC} ${TOMMATH_HPP})

set_property(TARGET "tommath" PROPERTY FOLDER "External Libraries")

# tommath 1.2.0 depends on dead code elimination
if(MSVC)
  # with MSVC the only way I could find to enable dead code elimination is to
  # enable optimization altogether. This requires to disable RTC (run-time
  # error checks) though, because those two options are not compatible.
  string(REGEX REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
  target_compile_options("tommath" PRIVATE "/Ox")
else()
  target_compile_options("tommath" PRIVATE "-Og")
endif()

disable_project_warnings("tommath")

target_include_directories("tommath" PUBLIC "libtommath")

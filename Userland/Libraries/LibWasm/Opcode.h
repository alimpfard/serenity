/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>

namespace Wasm {

TYPEDEF_DISTINCT_ORDERED_ID(u32, OpCode);

namespace Instructions {

#define ENUMERATE_WASM_SINGLE_BYTE_INSTRUCTIONS(M) \
    M(0x00, unreachable)                           \
    M(0x01, nop)                                   \
    M(0x02, block)                                 \
    M(0x03, loop)                                  \
    M(0x04, if_)                                   \
    M(0x0c, br)                                    \
    M(0x0d, br_if)                                 \
    M(0x0e, br_table)                              \
    M(0x0f, return_)                               \
    M(0x10, call)                                  \
    M(0x11, call_indirect)                         \
    M(0x1a, drop)                                  \
    M(0x1b, select)                                \
    M(0x1c, select_typed)                          \
    M(0x20, local_get)                             \
    M(0x21, local_set)                             \
    M(0x22, local_tee)                             \
    M(0x23, global_get)                            \
    M(0x24, global_set)                            \
    M(0x25, table_get)                             \
    M(0x26, table_set)                             \
    M(0x28, i32_load)                              \
    M(0x29, i64_load)                              \
    M(0x2a, f32_load)                              \
    M(0x2b, f64_load)                              \
    M(0x2c, i32_load8_s)                           \
    M(0x2d, i32_load8_u)                           \
    M(0x2e, i32_load16_s)                          \
    M(0x2f, i32_load16_u)                          \
    M(0x30, i64_load8_s)                           \
    M(0x31, i64_load8_u)                           \
    M(0x32, i64_load16_s)                          \
    M(0x33, i64_load16_u)                          \
    M(0x34, i64_load32_s)                          \
    M(0x35, i64_load32_u)                          \
    M(0x36, i32_store)                             \
    M(0x37, i64_store)                             \
    M(0x38, f32_store)                             \
    M(0x39, f64_store)                             \
    M(0x3a, i32_store8)                            \
    M(0x3b, i32_store16)                           \
    M(0x3c, i64_store8)                            \
    M(0x3d, i64_store16)                           \
    M(0x3e, i64_store32)                           \
    M(0x3f, memory_size)                           \
    M(0x40, memory_grow)                           \
    M(0x41, i32_const)                             \
    M(0x42, i64_const)                             \
    M(0x43, f32_const)                             \
    M(0x44, f64_const)                             \
    M(0x45, i32_eqz)                               \
    M(0x46, i32_eq)                                \
    M(0x47, i32_ne)                                \
    M(0x48, i32_lts)                               \
    M(0x49, i32_ltu)                               \
    M(0x4a, i32_gts)                               \
    M(0x4b, i32_gtu)                               \
    M(0x4c, i32_les)                               \
    M(0x4d, i32_leu)                               \
    M(0x4e, i32_ges)                               \
    M(0x4f, i32_geu)                               \
    M(0x50, i64_eqz)                               \
    M(0x51, i64_eq)                                \
    M(0x52, i64_ne)                                \
    M(0x53, i64_lts)                               \
    M(0x54, i64_ltu)                               \
    M(0x55, i64_gts)                               \
    M(0x56, i64_gtu)                               \
    M(0x57, i64_les)                               \
    M(0x58, i64_leu)                               \
    M(0x59, i64_ges)                               \
    M(0x5a, i64_geu)                               \
    M(0x5b, f32_eq)                                \
    M(0x5c, f32_ne)                                \
    M(0x5d, f32_lt)                                \
    M(0x5e, f32_gt)                                \
    M(0x5f, f32_le)                                \
    M(0x60, f32_ge)                                \
    M(0x61, f64_eq)                                \
    M(0x62, f64_ne)                                \
    M(0x63, f64_lt)                                \
    M(0x64, f64_gt)                                \
    M(0x65, f64_le)                                \
    M(0x66, f64_ge)                                \
    M(0x67, i32_clz)                               \
    M(0x68, i32_ctz)                               \
    M(0x69, i32_popcnt)                            \
    M(0x6a, i32_add)                               \
    M(0x6b, i32_sub)                               \
    M(0x6c, i32_mul)                               \
    M(0x6d, i32_divs)                              \
    M(0x6e, i32_divu)                              \
    M(0x6f, i32_rems)                              \
    M(0x70, i32_remu)                              \
    M(0x71, i32_and)                               \
    M(0x72, i32_or)                                \
    M(0x73, i32_xor)                               \
    M(0x74, i32_shl)                               \
    M(0x75, i32_shrs)                              \
    M(0x76, i32_shru)                              \
    M(0x77, i32_rotl)                              \
    M(0x78, i32_rotr)                              \
    M(0x79, i64_clz)                               \
    M(0x7a, i64_ctz)                               \
    M(0x7b, i64_popcnt)                            \
    M(0x7c, i64_add)                               \
    M(0x7d, i64_sub)                               \
    M(0x7e, i64_mul)                               \
    M(0x7f, i64_divs)                              \
    M(0x80, i64_divu)                              \
    M(0x81, i64_rems)                              \
    M(0x82, i64_remu)                              \
    M(0x83, i64_and)                               \
    M(0x84, i64_or)                                \
    M(0x85, i64_xor)                               \
    M(0x86, i64_shl)                               \
    M(0x87, i64_shrs)                              \
    M(0x88, i64_shru)                              \
    M(0x89, i64_rotl)                              \
    M(0x8a, i64_rotr)                              \
    M(0x8b, f32_abs)                               \
    M(0x8c, f32_neg)                               \
    M(0x8d, f32_ceil)                              \
    M(0x8e, f32_floor)                             \
    M(0x8f, f32_trunc)                             \
    M(0x90, f32_nearest)                           \
    M(0x91, f32_sqrt)                              \
    M(0x92, f32_add)                               \
    M(0x93, f32_sub)                               \
    M(0x94, f32_mul)                               \
    M(0x95, f32_div)                               \
    M(0x96, f32_min)                               \
    M(0x97, f32_max)                               \
    M(0x98, f32_copysign)                          \
    M(0x99, f64_abs)                               \
    M(0x9a, f64_neg)                               \
    M(0x9b, f64_ceil)                              \
    M(0x9c, f64_floor)                             \
    M(0x9d, f64_trunc)                             \
    M(0x9e, f64_nearest)                           \
    M(0x9f, f64_sqrt)                              \
    M(0xa0, f64_add)                               \
    M(0xa1, f64_sub)                               \
    M(0xa2, f64_mul)                               \
    M(0xa3, f64_div)                               \
    M(0xa4, f64_min)                               \
    M(0xa5, f64_max)                               \
    M(0xa6, f64_copysign)                          \
    M(0xa7, i32_wrap_i64)                          \
    M(0xa8, i32_trunc_sf32)                        \
    M(0xa9, i32_trunc_uf32)                        \
    M(0xaa, i32_trunc_sf64)                        \
    M(0xab, i32_trunc_uf64)                        \
    M(0xac, i64_extend_si32)                       \
    M(0xad, i64_extend_ui32)                       \
    M(0xae, i64_trunc_sf32)                        \
    M(0xaf, i64_trunc_uf32)                        \
    M(0xb0, i64_trunc_sf64)                        \
    M(0xb1, i64_trunc_uf64)                        \
    M(0xb2, f32_convert_si32)                      \
    M(0xb3, f32_convert_ui32)                      \
    M(0xb4, f32_convert_si64)                      \
    M(0xb5, f32_convert_ui64)                      \
    M(0xb6, f32_demote_f64)                        \
    M(0xb7, f64_convert_si32)                      \
    M(0xb8, f64_convert_ui32)                      \
    M(0xb9, f64_convert_si64)                      \
    M(0xba, f64_convert_ui64)                      \
    M(0xbb, f64_promote_f32)                       \
    M(0xbc, i32_reinterpret_f32)                   \
    M(0xbd, i64_reinterpret_f64)                   \
    M(0xbe, f32_reinterpret_i32)                   \
    M(0xbf, f64_reinterpret_i64)                   \
    M(0xc0, i32_extend8_s)                         \
    M(0xc1, i32_extend16_s)                        \
    M(0xc2, i64_extend8_s)                         \
    M(0xc3, i64_extend16_s)                        \
    M(0xc4, i64_extend32_s)                        \
    M(0xd0, ref_null)                              \
    M(0xd1, ref_is_null)                           \
    M(0xd2, ref_func)

// These are synthetic opcodes, they are _not_ seen in wasm with these values.
#define ENUMERATE_WASM_MULTI_BYTE_INSTRUCTIONS(M) \
    M(0xfc00, i32_trunc_sat_f32_s)                \
    M(0xfc01, i32_trunc_sat_f32_u)                \
    M(0xfc02, i32_trunc_sat_f64_s)                \
    M(0xfc03, i32_trunc_sat_f64_u)                \
    M(0xfc04, i64_trunc_sat_f32_s)                \
    M(0xfc05, i64_trunc_sat_f32_u)                \
    M(0xfc06, i64_trunc_sat_f64_s)                \
    M(0xfc07, i64_trunc_sat_f64_u)                \
    M(0xfc08, memory_init)                        \
    M(0xfc09, data_drop)                          \
    M(0xfc0a, memory_copy)                        \
    M(0xfc0b, memory_fill)                        \
    M(0xfc0c, table_init)                         \
    M(0xfc0d, elem_drop)                          \
    M(0xfc0e, table_copy)                         \
    M(0xfc0f, table_grow)                         \
    M(0xfc10, table_size)                         \
    M(0xfc11, table_fill)                         \
    M(0xff00, structured_else)                    \
    M(0xff01, structured_end)

#define M(value, name) \
    static constexpr OpCode name = (value);

ENUMERATE_WASM_SINGLE_BYTE_INSTRUCTIONS(M);
ENUMERATE_WASM_MULTI_BYTE_INSTRUCTIONS(M);

#undef M

static constexpr u32 i32_trunc_sat_f32_s_second = 0,
                     i32_trunc_sat_f32_u_second = 1,
                     i32_trunc_sat_f64_s_second = 2,
                     i32_trunc_sat_f64_u_second = 3,
                     i64_trunc_sat_f32_s_second = 4,
                     i64_trunc_sat_f32_u_second = 5,
                     i64_trunc_sat_f64_s_second = 6,
                     i64_trunc_sat_f64_u_second = 7,
                     memory_init_second = 8,
                     data_drop_second = 9,
                     memory_copy_second = 10,
                     memory_fill_second = 11,
                     table_init_second = 12,
                     elem_drop_second = 13,
                     table_copy_second = 14,
                     table_grow_second = 15,
                     table_size_second = 16,
                     table_fill_second = 17;

}

}

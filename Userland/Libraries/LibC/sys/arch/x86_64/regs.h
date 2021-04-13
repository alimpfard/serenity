/*
 * Copyright (c) 2021, Leon Albrecht <leon2002.la@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <AK/_Types.h>

#define RREGISTER(num)                               \
    union {                                          \
        __serenity_u64 r##num;                       \
        struct {                                     \
            __serenity_u32 _;                        \
            union {                                  \
                __serenity_u32 r##num##d;            \
                struct {                             \
                    __serenity_u16 __;               \
                    union {                          \
                        __serenity_u16 r##num##w;    \
                        struct {                     \
                            __serenity_u8 ___;       \
                            __serenity_u8 r##num##b; \
                        };                           \
                    };                               \
                };                                   \
            };                                       \
        };                                           \
    }

#define GPREGISTER(letter)                           \
    union {                                          \
        __serenity_u64 r##letter##x;                 \
        struct                                       \
        {                                            \
            __serenity_u32 _;                        \
            union {                                  \
                __serenity_u32 e##letter##x;         \
                struct                               \
                {                                    \
                    __serenity_u16 __;               \
                    union {                          \
                        __serenity_u16 letter##x;    \
                        struct {                     \
                            __serenity_u8 letter##h; \
                            __serenity_u8 letter##l; \
                        };                           \
                    };                               \
                };                                   \
            };                                       \
        };                                           \
    }

#define SPREGISTER(name)                           \
    union {                                        \
        __serenity_u64 r##name;                    \
        struct                                     \
        {                                          \
            __serenity_u32 _;                      \
            union {                                \
                __serenity_u32 e##name;            \
                struct                             \
                {                                  \
                    __serenity_u16 __;             \
                    union {                        \
                        __serenity_u16 name;       \
                        struct {                   \
                            __serenity_u8 ___;     \
                            __serenity_u8 name##l; \
                        };                         \
                    };                             \
                };                                 \
            };                                     \
        };                                         \
    }

struct __attribute__((packed)) PtraceRegisters {
    GPREGISTER(a);
    GPREGISTER(b);
    GPREGISTER(c);
    GPREGISTER(d);

    SPREGISTER(sp);
    SPREGISTER(bp);
    SPREGISTER(si);
    SPREGISTER(di);
    SPREGISTER(ip); // technically there is no ipl, but what ever

    RREGISTER(8);
    RREGISTER(9);
    RREGISTER(10);
    RREGISTER(11);
    RREGISTER(12);
    RREGISTER(13);
    RREGISTER(14);
    RREGISTER(15);
    // flags
    union {
        __serenity_u64 rflags;
        struct {
            __serenity_u32 _;
            union {
                __serenity_u32 eflags;
                struct {
                    __serenity_u16 __;
                    __serenity_u16 flags;
                };
            };
        };
    };

    // These may not be used, unless we go back into compatability mode
    __serenity_u32 cs;
    __serenity_u32 ss;
    __serenity_u32 ds;
    __serenity_u32 es;
    __serenity_u32 fs;
    __serenity_u32 gs;

    // FIXME: Add FPU registers and Flags
    // FIXME: Add Ymm Xmm etc.
};

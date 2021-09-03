/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibRegex/Forward.h>

namespace regex {

class Debugger {
public:
    Debugger() = default;
    virtual ~Debugger() = default;

    virtual void enter_match() { }
    virtual void leave_match() { }
    virtual void enter_opcode(OpCode const&, MatchState const&, [[maybe_unused]] size_t recursion_level) { }
    virtual void leave_opcode(OpCode const&, ByteCode const&, MatchInput const&, MatchState const&, ExecutionResult) { }
};

}

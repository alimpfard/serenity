/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/NodePool.h>

namespace JS {

NodePool& NodePool::the()
{
    static NodePool s_pool;
    return s_pool;
}

}

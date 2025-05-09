/*
 * Copyright (c) 2025, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Cell.h"
#include "Type.h"

namespace Spreadsheet {

class PlotCell : public CellType {
public:
    PlotCell();
    virtual ~PlotCell() override = default;
    virtual JS::ThrowCompletionOr<ByteString> display(Cell&, CellTypeMetadata const&) const override;
    virtual JS::ThrowCompletionOr<JS::Value> js_value(Cell&, CellTypeMetadata const&) const override;
    virtual String metadata_hint(MetadataName) const override;
};

}

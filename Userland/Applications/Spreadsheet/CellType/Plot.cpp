/*
 * Copyright (c) 2025, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "../Cell.h"
#include "../Spreadsheet.h"
#include "Format.h"
#include "Plot.h"
#include <AK/ScopeGuard.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/Completion.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibJS/Runtime/FunctionObject.h>

namespace Spreadsheet {

PlotCell::PlotCell()
    : CellType("Plot"sv)
{
}

JS::ThrowCompletionOr<ByteString> PlotCell::display(Cell& cell, CellTypeMetadata const&) const
{
    auto& vm = cell.sheet().global_object().vm();
    auto plot_object = TRY(cell.evaluated_data().to_object(vm));

    auto to_json_object = TRY(plot_object->get("toJSON"));
    if (!to_json_object.is_function())
        return vm.throw_completion<JS::InternalError>("<plot>.toJSON() is not a function"sv);

    auto to_json = &to_json_object.as_function();
    auto maybe_json = TRY(to_json->internal_call(plot_object, {}));
    if (!maybe_json.is_string())
        return vm.throw_completion<JS::InternalError>("Plot.toJSON() did not return a string"sv);

    auto json = maybe_json.as_string().byte_string();
    dbgln("Plot.toJSON() = {}", json);
    return json;
}

JS::ThrowCompletionOr<JS::Value> PlotCell::js_value(Cell& cell, CellTypeMetadata const&) const
{
    return cell.js_data();
}

String PlotCell::metadata_hint(MetadataName) const
{
    return {};
}

}

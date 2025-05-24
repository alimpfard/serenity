/*
 * Copyright (c) 2023, Jelle Raaijmakers <jelle@gmta.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Devices/Audio/IntelHDA/Controller.h>
#include <Kernel/Devices/Audio/IntelHDA/InterruptHandler.h>

namespace Kernel::Audio::IntelHDA {

InterruptHandler::InterruptHandler(Controller& controller)
    : PCI::IRQHandler(controller, controller.device_identifier().interrupt_line().value())
    , m_controller(controller)
{
    enable_irq();
}

ErrorOr<void> InterruptHandler::validate_irq(Controller const& controller)
{
    if (controller.device_identifier().interrupt_line().value() >= GENERIC_INTERRUPT_HANDLERS_COUNT)
        return ENODEV;
    return {};
}

bool InterruptHandler::handle_irq()
{
    auto result_or_error = m_controller.handle_interrupt({});
    if (result_or_error.is_error()) {
        dmesgln("IntelHDA: Error during interrupt handling: {}", result_or_error.release_error());
        return false;
    }
    return result_or_error.release_value();
}

}

/*
 * Copyright (c) 2025, Sönke Holz <sholz8530@gmail.com>
 * Copyright (c) 2025, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Kernel/Time/TimerQueue.h"

#include <Kernel/Devices/Input/HID/ApplicationCollectionDriver.h>
#include <Kernel/Devices/Input/MouseDevice.h>

namespace Kernel::HID {

class TouchDriver final : public ApplicationCollectionDriver {
public:
    virtual ~TouchDriver() override;
    static ErrorOr<NonnullRefPtr<TouchDriver>> create(Device const&, ::HID::ApplicationCollection const&);

private:
    TouchDriver(Device const&, ::HID::ApplicationCollection const&, NonnullRefPtr<MouseDevice>);

    // ^ApplicationCollectionDriver
    virtual ErrorOr<void> on_report(ReadonlyBytes) override;

    NonnullRefPtr<MouseDevice> m_mouse_device;

    struct TouchState {
        MousePacket packet {};
        u32 width { 0 };
        u32 height { 0 };
    };
    HashMap<i64, TouchState> m_state_per_identifier {};
    u64 m_current_scan_time { 0 }; // in 100us
    i64 m_delta_time { 1 }; // in 100us
    RefPtr<Timer> m_release_timer;
};

}

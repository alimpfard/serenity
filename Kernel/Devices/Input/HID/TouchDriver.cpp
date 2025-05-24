/*
 * Copyright (c) 2025, Sönke Holz <sholz8530@gmail.com>
 * Copyright (c) 2025, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Devices/Input/HID/Definitions.h>
#include <Kernel/Devices/Input/HID/Device.h>
#include <Kernel/Devices/Input/HID/TouchDriver.h>
#include <Kernel/Devices/Input/Management.h>

#include <LibHID/ReportParser.h>

namespace Kernel::HID {

TouchDriver::~TouchDriver()
{
    InputManagement::the().detach_standalone_input_device(*m_mouse_device);
}

ErrorOr<NonnullRefPtr<TouchDriver>> TouchDriver::create(Device const& device, ::HID::ApplicationCollection const& application_collection)
{
    auto mouse_device = TRY(::MouseDevice::try_to_initialize());
    auto handler = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) TouchDriver(device, application_collection, move(mouse_device))));
    InputManagement::the().attach_standalone_input_device(*handler->m_mouse_device);
    return handler;
}

TouchDriver::TouchDriver(Device const& device, ::HID::ApplicationCollection const& application_collection, NonnullRefPtr<::MouseDevice> mouse_device)
    : ApplicationCollectionDriver(device, application_collection)
    , m_mouse_device(move(mouse_device))
{
}

ErrorOr<void> TouchDriver::on_report(ReadonlyBytes report_data)
{
    if (m_release_timer) {
        dbgln("Got new touch report, canceling release timer");
        TimerQueue::the().cancel_timer(*m_release_timer);
    }
    TouchState temp_state {};
    temp_state.packet.is_relative = false;
    auto* touch_state = &temp_state;
    Vector<i64> seen_touches;

    decltype(m_state_per_identifier) current_state_per_identifier;

    TRY(::HID::parse_input_report(m_device.report_descriptor(), m_application_collection, report_data, [this, &touch_state, &seen_touches, &current_state_per_identifier](::HID::Field const& field, i64 value) mutable -> ErrorOr<IterationDecision> {
        u32 usage = 0;
        if (field.is_array) {
            if (!field.usage_minimum.has_value())
                return Error::from_errno(ENOTSUP); // TODO: What are we supposed to do here?

            usage = value + field.usage_minimum.value();
            value = 1;
        } else {
            usage = field.usage.value();
        }

        using enum Usage;
        switch (static_cast<Usage>(usage)) {
        case X:
            touch_state->packet.is_relative = field.is_relative;
            if (field.is_relative)
                touch_state->packet.x = value;
            else
                touch_state->packet.x = static_cast<int>((value - field.logical_minimum) * 0xffff / (field.logical_maximum - field.logical_minimum));

            break;

        case Y:
            touch_state->packet.is_relative = field.is_relative;
            if (field.is_relative)
                touch_state->packet.y = -value;
            else
                touch_state->packet.y = static_cast<int>((value - field.logical_minimum) * 0xffff / (field.logical_maximum - field.logical_minimum));

            break;

        case ContactIdentifier:
            touch_state = &current_state_per_identifier.ensure(value, [&] { return *touch_state; });
            seen_touches.append(value);
            break;

        case ContactCount:
            seen_touches.remove(value, seen_touches.size() - value);
            break;

        case TipSwitch:
            if (value != !!(touch_state->packet.buttons & MousePacket::LeftButton))
                touch_state->packet.buttons ^= MousePacket::LeftButton;
            break;

        case ScanTime:
            if (m_current_scan_time == 0)
                m_current_scan_time = value;
            m_delta_time = value - m_current_scan_time;
            m_current_scan_time = value;
            break;

        default:
            dbgln_if(HID_DEBUG, "HID: Unknown Mouse Application Collection Usage: {:#x}", usage);
            break;
        }

        return IterationDecision::Continue;
    }));

    dmesgln("Touch report processed!");
    if (m_delta_time <= 0)
        m_delta_time = 1; // Nothing happens in 0 time.

    size_t count_held = 0;
    MousePacket emulated { .is_relative = false };
    HashTable<i64> active_touches;
    HashTable<i64> to_remove;
    for (auto& [id, state] : current_state_per_identifier) {
        if (!seen_touches.contains_slow(id)) {
            if (m_state_per_identifier.contains(id))
                to_remove.set(id);
            continue;
        }

        count_held++;
        active_touches.set(id);
        // dmesgln("TouchDriver: touch id {} seen at {} {}", id, state.packet.x, state.packet.y);
        switch (count_held) {
        case 1:
            emulated.buttons |= MousePacket::Button::LeftButton;
            emulated.x = state.packet.x;
            emulated.y = state.packet.y;
            break;
        case 2: {
            i64 ids[2];
            size_t idx = 0;
            for (auto id : active_touches) {
                if (idx < 2)
                    ids[idx++] = id;
            }
            auto& s1 = current_state_per_identifier.get(ids[0]).value();
            auto& s2 = current_state_per_identifier.get(ids[1]).value();

            auto avg_x = (s1.packet.x + s2.packet.x) / 2;
            auto avg_y = (s1.packet.y + s2.packet.y) / 2;

            int prev_avg_x = 0, prev_avg_y = 0;
            if (auto prev1 = m_state_per_identifier.get(ids[0]), prev2 = m_state_per_identifier.get(ids[1]);
                prev1.has_value() && prev2.has_value()) {
                prev_avg_x = (prev1->packet.x + prev2->packet.x) / 2;
                prev_avg_y = (prev1->packet.y + prev2->packet.y) / 2;
            } else {
                prev_avg_x = avg_x;
                prev_avg_y = avg_y;
            }

            int delta_x = avg_x - prev_avg_x;
            int delta_y = avg_y - prev_avg_y;

            int dx = s1.packet.x - s2.packet.x;
            int dy = s1.packet.y - s2.packet.y;
            int distance = dx * dx + dy * dy;

            auto prev1 = m_state_per_identifier.get(ids[0]);
            auto prev2 = m_state_per_identifier.get(ids[1]);

            int prev_dx = prev1.has_value() && prev2.has_value()
                ? (prev1->packet.x - prev2->packet.x)
                : dx;
            int prev_dy = prev1.has_value() && prev2.has_value()
                ? (prev1->packet.y - prev2->packet.y)
                : dy;
            int prev_distance = prev_dx * prev_dx + prev_dy * prev_dy;

            int distance_delta = distance - prev_distance;

            constexpr int scroll_sensitivity = 600; // arbitrary, tune as needed.

            // Holding one finger and moving the other finger, scroll in both directions.
            if (distance_delta > scroll_sensitivity || distance_delta < -scroll_sensitivity) {
                emulated.buttons = 0;
                emulated.z = prev_dx - dx;
                emulated.w = dy - prev_dy;
                emulated.z *= (emulated.z > 0 ? 1 : -1) * emulated.z;
                emulated.w *= (emulated.w > 0 ? 1 : -1) * emulated.w;
                emulated.z /= scroll_sensitivity;
                emulated.w /= scroll_sensitivity;
                emulated.x = 0;
                emulated.y = 0;
                emulated.is_relative = true;
            } else {
                // Moving both fingers, pretend it is a mouse move :P
                emulated.buttons = 0;
                emulated.x = delta_x * delta_x / scroll_sensitivity;
                emulated.y = delta_y * delta_y / scroll_sensitivity;
                emulated.x *= delta_x > 0 ? 1 : -1;
                emulated.y *= delta_y > 0 ? -1 : 1;
                emulated.is_relative = true;
            }
            break;
        }
        case 3:
            // middle click :shrug:
            emulated.buttons |= MousePacket::Button::MiddleButton;
            state.packet = emulated;
            break;
        default:
            break;
        }
    }

    if (count_held == 0) {
        // Nothing is held, release all buttons.
        for (auto& entry : m_state_per_identifier)
            to_remove.set(entry.key);
    }

    if (to_remove.is_empty() && count_held == 0) {
        m_state_per_identifier = move(current_state_per_identifier);
        return {};
    }

    for (auto id : to_remove) {
        auto entry = m_state_per_identifier.take(id);
        if (!entry.has_value())
            continue;

        auto packet = entry->packet;
        if (!packet.buttons)
            continue;

        packet.buttons = 0;
        m_mouse_device->handle_mouse_packet_input_event(entry->packet);
    }

    m_state_per_identifier = move(current_state_per_identifier);

    if (count_held > 0) {
        dmesgln("TouchDriver: Emulating {} touches, delta time: {}us", count_held, m_delta_time * 100);
        dmesgln("TouchDriver: Emulated packet: buttons: {}, x: {}, y: {}, z: {}, w: {}, is_relative: {}",
            emulated.buttons, emulated.x, emulated.y, emulated.z, emulated.w, emulated.is_relative);
        m_mouse_device->handle_mouse_packet_input_event(emulated);

        auto const deadline = TimeManagement::the().current_time(CLOCK_MONOTONIC) + Duration::from_microseconds(m_delta_time * 100 * 10);
        auto release_timer = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) Timer()));
        release_timer->setup(CLOCK_MONOTONIC, deadline, [this]() {
            if (m_state_per_identifier.is_empty())
                return;

            dmesgln("TouchDriver: Releasing all held touches ({})", m_state_per_identifier.size());
            m_state_per_identifier.clear();
            m_release_timer.clear();
            m_mouse_device->handle_mouse_packet_input_event({});
        });
        m_release_timer = release_timer;
        TimerQueue::the().add_timer(move(release_timer));
    }

    return {};
}

}

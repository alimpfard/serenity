/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// IPC encode/decode for WindowServer::ScreenLayout, extracted from ScreenLayout.ipp
// without the Serenity-kernel-dependent parts.

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <WindowServer/ScreenLayout.h>

namespace WindowServer {

bool ScreenLayout::is_valid(ByteString*) const { return true; }
bool ScreenLayout::normalize() { return false; }
bool ScreenLayout::load_config(Core::ConfigFile const&, ByteString*) { return false; }
bool ScreenLayout::save_config(Core::ConfigFile&, bool) const { return false; }
bool ScreenLayout::try_auto_add_display_connector(ByteString const&) { return false; }

bool ScreenLayout::operator!=(ScreenLayout const& other) const
{
    if (this == &other)
        return false;
    if (main_screen_index != other.main_screen_index)
        return true;
    if (screens.size() != other.screens.size())
        return true;
    for (size_t i = 0; i < screens.size(); i++) {
        if (screens[i] != other.screens[i])
            return true;
    }
    return false;
}

}

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, WindowServer::ScreenLayout::Screen const& screen)
{
    TRY(encoder.encode(screen.mode));
    TRY(encoder.encode(screen.device));
    TRY(encoder.encode(screen.location));
    TRY(encoder.encode(screen.resolution));
    TRY(encoder.encode(screen.scale_factor));
    return {};
}

template<>
ErrorOr<WindowServer::ScreenLayout::Screen> decode(Decoder& decoder)
{
    auto mode = TRY(decoder.decode<WindowServer::ScreenLayout::Screen::Mode>());
    auto device = TRY(decoder.decode<Optional<ByteString>>());
    auto location = TRY(decoder.decode<Gfx::IntPoint>());
    auto resolution = TRY(decoder.decode<Gfx::IntSize>());
    auto scale_factor = TRY(decoder.decode<int>());
    return WindowServer::ScreenLayout::Screen { mode, device, location, resolution, scale_factor };
}

template<>
ErrorOr<void> encode(Encoder& encoder, WindowServer::ScreenLayout const& screen_layout)
{
    TRY(encoder.encode(screen_layout.screens));
    TRY(encoder.encode(screen_layout.main_screen_index));
    return {};
}

template<>
ErrorOr<WindowServer::ScreenLayout> decode(Decoder& decoder)
{
    auto screens = TRY(decoder.decode<Vector<WindowServer::ScreenLayout::Screen>>());
    auto main_screen_index = TRY(decoder.decode<unsigned>());
    return WindowServer::ScreenLayout { move(screens), main_screen_index };
}

}

/*
 * Copyright (c) 2020, the SerenityOS developers.
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

#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/String.h>
#include <AK/Types.h>
#include <Kernel/API/KeyCode.h>
#include <Kernel/API/MousePacket.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibIPC/ServerConnection.h>
#include <LibKeyboard/CharacterMapFile.h>
#include <WindowServer/WindowClientEndpoint.h>
#include <WindowServer/WindowServerEndpoint.h>

static auto read_keymap()
{
    auto proc_keymap = Core::File::construct("/proc/keymap");
    if (!proc_keymap->open(Core::IODevice::OpenMode::ReadOnly))
        ASSERT_NOT_REACHED();

    auto json = JsonValue::from_string(proc_keymap->read_all());
    ASSERT(json.has_value());
    JsonObject keymap_object = json.value().as_object();
    ASSERT(keymap_object.has("keymap"));
    String current_keymap_name = keymap_object.get("keymap").to_string();
    auto keymap_result = Keyboard::CharacterMapFile::load_from_file(String::formatted("/res/keymaps/{}.json", current_keymap_name));
    ASSERT(keymap_result.has_value());
    return keymap_result.value();
}

inline KeyCode key_code_from_string(const StringView& name)
{
#define __ENUMERATE_KEY_CODE(key_name, ui_name)            \
    if (StringView { ui_name }.equals_ignoring_case(name)) \
        return Key_##key_name;
    ENUMERATE_KEY_CODES
#undef __ENUMERATE_KEY_CODE
    return Key_Invalid;
}

class WindowServerConnection
    : public IPC::ServerConnection<WindowClientEndpoint, WindowServerEndpoint>
    , public WindowClientEndpoint {
    C_OBJECT(WindowServerConnection)
public:
    WindowServerConnection()
        : IPC::ServerConnection<WindowClientEndpoint, WindowServerEndpoint>(*this, "/tmp/portal/window")
    {
        handshake();

        auto response = send_sync<Messages::WindowServer::StartScriptedControlSession>();
        m_id = response->session_id();
    }

    ~WindowServerConnection()
    {
        auto response = send_sync<Messages::WindowServer::EndScriptedControlSession>(m_id);
    }

    virtual void handshake() override
    {
        auto response = send_sync<Messages::WindowServer::Greet>();
        set_my_client_id(response->client_id());
    }

    void send_key_event(const KeyEvent& event)
    {
        post_message(Messages::WindowServer::EnqueueScriptedKeyboardEvent(m_id, event.key, event.scancode, event.code_point, event.flags, event.caps_lock_on, event.e0_prefix));
    }

    void send_mouse_event(const MousePacket& event)
    {
        post_message(Messages::WindowServer::EnqueueScriptedMouseEvent(m_id, event.x, event.y, event.z, event.buttons, event.is_relative));
    }

private:
    virtual void handle(const Messages::WindowClient::Paint&) override { }
    virtual void handle(const Messages::WindowClient::MouseMove&) override { }
    virtual void handle(const Messages::WindowClient::MouseDown&) override { }
    virtual void handle(const Messages::WindowClient::MouseDoubleClick&) override { }
    virtual void handle(const Messages::WindowClient::MouseUp&) override { }
    virtual void handle(const Messages::WindowClient::MouseWheel&) override { }
    virtual void handle(const Messages::WindowClient::WindowEntered&) override { }
    virtual void handle(const Messages::WindowClient::WindowLeft&) override { }
    virtual void handle(const Messages::WindowClient::KeyDown&) override { }
    virtual void handle(const Messages::WindowClient::KeyUp&) override { }
    virtual void handle(const Messages::WindowClient::WindowActivated&) override { }
    virtual void handle(const Messages::WindowClient::WindowDeactivated&) override { }
    virtual void handle(const Messages::WindowClient::WindowInputEntered&) override { }
    virtual void handle(const Messages::WindowClient::WindowInputLeft&) override { }
    virtual void handle(const Messages::WindowClient::WindowCloseRequest&) override { }
    virtual void handle(const Messages::WindowClient::WindowResized&) override { }
    virtual void handle(const Messages::WindowClient::MenuItemActivated&) override { }
    virtual void handle(const Messages::WindowClient::ScreenRectChanged&) override { }
    virtual void handle(const Messages::WindowClient::WM_WindowRemoved&) override { }
    virtual void handle(const Messages::WindowClient::WM_WindowStateChanged&) override { }
    virtual void handle(const Messages::WindowClient::WM_WindowIconBitmapChanged&) override { }
    virtual void handle(const Messages::WindowClient::WM_WindowRectChanged&) override { }
    virtual void handle(const Messages::WindowClient::AsyncSetWallpaperFinished&) override { }
    virtual void handle(const Messages::WindowClient::DragDropped&) override { }
    virtual void handle(const Messages::WindowClient::DragAccepted&) override { }
    virtual void handle(const Messages::WindowClient::DragCancelled&) override { }
    virtual void handle(const Messages::WindowClient::UpdateSystemTheme&) override { }
    virtual void handle(const Messages::WindowClient::WindowStateChanged&) override { }
    virtual void handle(const Messages::WindowClient::DisplayLinkNotification&) override { }
    virtual void handle(const Messages::WindowClient::Ping&) override { }

    int m_id { -1 };
};

struct UnresolvedKeyEvent {
    KeyCode key_code;
    u32 scancode { 0 };
    u32 code_point { 0 };
    u8 flags { 0 };
    String key_str;
};
static ::KeyEvent resolve(const UnresolvedKeyEvent& uevent, const Keyboard::CharacterMapData& keymap)
{
    KeyEvent event;
    event.flags = uevent.flags;
    event.key = uevent.key_code;
    const decltype(keymap.map)* active_map;
    if (event.shift())
        active_map = &keymap.shift_map;
    else if (event.alt())
        active_map = &keymap.alt_map;
    else if (event.altgr())
        active_map = &keymap.altgr_map;
    else
        active_map = &keymap.map;

    auto& map = *active_map;
    bool found_one = false;
    for (size_t i = 0; i < array_size(keymap.map); ++i) {
        if ((char)map[i] == uevent.key_str[0]) {
            found_one = true;
            event.scancode = i;
            event.code_point = uevent.key_str[0];
            break;
        }
    }

    if (!found_one)
        dbgln("Unresolved key event for key name '{}'", uevent.key_str);

    return event;
}

static Optional<u8> find_required_flags_for(char ch, const Keyboard::CharacterMapData& keymap)
{
    for (size_t i = 0; i < array_size(keymap.map); ++i) {
        if ((char)keymap.map[i] == ch)
            return 0;
    }
    for (size_t i = 0; i < array_size(keymap.alt_map); ++i) {
        if ((char)keymap.alt_map[i] == ch)
            return Mod_Alt;
    }
    for (size_t i = 0; i < array_size(keymap.altgr_map); ++i) {
        if ((char)keymap.altgr_map[i] == ch)
            return Mod_AltGr;
    }
    for (size_t i = 0; i < array_size(keymap.shift_map); ++i) {
        if ((char)keymap.shift_map[i] == ch)
            return Mod_Shift;
    }
    return {};
}

auto main(int argc, char* argv[]) -> int
{
    enum Order {
        Key,
        Mouse,
    };

    Vector<Order> event_order;
    Vector<KeyEvent> finished_key_events;
    Vector<MousePacket> finished_mouse_packets;
    UnresolvedKeyEvent pending_key_event;
    MousePacket pending_mouse_packet;

    bool has_started_mouse_packet = false;
    bool has_started_keyboard_event = false;
    int delay = 20;

    Optional<Keyboard::CharacterMapData> keymap_;
    auto keymap = [&]() -> Keyboard::CharacterMapData& {
        if (keymap_.has_value())
            return keymap_.value();
        keymap_ = read_keymap();
        return keymap_.value();
    };

    Core::EventLoop loop;
    Core::ArgsParser parser;

    {
        parser.add_option(delay, "Time to wait between each event in ms (default=10ms)", "delay", 0, "time");
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Start a mouse event descriptor",
            .long_name = "mouse",
            .accept_value = [&](auto*) -> bool {
                if (has_started_mouse_packet) {
                    event_order.append(Mouse);
                    finished_mouse_packets.append(pending_mouse_packet);
                    pending_mouse_packet = {};
                    return true;
                } else {
                    has_started_mouse_packet = true;
                    return true;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Make the current mouse packet absolute",
            .long_name = "absolute",
            .accept_value = [&](auto*) -> bool {
                if (has_started_mouse_packet) {
                    pending_mouse_packet.is_relative = false;
                    return true;
                } else {
                    warnln("\033[31No active mouse packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Set the current mouse packet's x coordinate",
            .short_name = 'x',
            .accept_value = [&](auto* s) -> bool {
                auto opt = StringView(s).to_int();
                if (has_started_mouse_packet && opt.has_value()) {
                    pending_mouse_packet.x = opt.value();
                    return true;
                } else if (!opt.has_value()) {
                    return false;
                } else {
                    warnln("\033[31No active mouse packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Set the current mouse packet's y coordinate",
            .short_name = 'y',
            .accept_value = [&](auto* s) -> bool {
                auto opt = StringView(s).to_int();
                if (has_started_mouse_packet && opt.has_value()) {
                    pending_mouse_packet.y = opt.value();
                    return true;
                } else if (!opt.has_value()) {
                    return false;
                } else {
                    warnln("\033[31No active mouse packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Set the current mouse packet's scroll value (z coordinate)",
            .long_name = "scroll",
            .short_name = 'z',
            .accept_value = [&](auto* s) -> bool {
                auto opt = StringView(s).to_int();
                if (has_started_mouse_packet && opt.has_value()) {
                    pending_mouse_packet.z = opt.value();
                    return true;
                } else if (!opt.has_value()) {
                    return false;
                } else {
                    warnln("\033[31No active mouse packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Register a mousedown on a given button for the current mouse packet (left|right|middle|back|forward)",
            .long_name = "click",
            .accept_value = [&](auto* s) -> bool {
                auto value = StringView(s);
                Optional<MousePacket::Button> opt;
                if (value == "left")
                    opt = MousePacket::Button::LeftButton;
                else if (value == "right")
                    opt = MousePacket::Button::RightButton;
                else if (value == "middle")
                    opt = MousePacket::Button::MiddleButton;
                else if (value == "back")
                    opt = MousePacket::Button::BackButton;
                else if (value == "forward")
                    opt = MousePacket::Button::ForwardButton;

                if (has_started_mouse_packet && opt.has_value()) {
                    pending_mouse_packet.buttons |= opt.value();
                    return true;
                } else if (!opt.has_value()) {
                    return false;
                } else {
                    warnln("\033[31No active mouse packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Start a keyboard event descriptor",
            .long_name = "keyboard",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    event_order.append(Key);
                    finished_key_events.append(resolve(pending_key_event, keymap()));
                    pending_key_event = {};
                    return true;
                } else {
                    has_started_keyboard_event = true;
                    return true;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Add a ctrl modifier to the current key event",
            .long_name = "ctrl",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags |= Mod_Ctrl;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Add a alt modifier to the current key event",
            .long_name = "alt",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags |= Mod_Alt;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Add a altgr modifier to the current key event",
            .long_name = "altgr",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags |= Mod_AltGr;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Add a logo modifier to the current key event",
            .long_name = "logo",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags |= Mod_Logo;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Add a shift modifier to the current key event",
            .long_name = "shift",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags |= Mod_Shift;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = false,
            .help_string = "Make the current key event a release event",
            .long_name = "release",
            .accept_value = [&](auto*) -> bool {
                if (has_started_keyboard_event) {
                    pending_key_event.flags &= ~Is_Press;
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Set the pressed key of the current key event",
            .long_name = "key",
            .accept_value = [&](auto* s) -> bool {
                if (has_started_keyboard_event) {
                    if (!pending_key_event.key_str.is_null()) {
                        warnln("\033[31A keyboard packet can't have two keys, dude\033[0m");
                        return false;
                    }
                    auto name = StringView { s };
                    pending_key_event.key_str = name;
                    pending_key_event.key_code = key_code_from_string(name);
                    pending_key_event.flags |= Is_Press;
                    if (pending_key_event.key_code == Key_Invalid) {
                        warnln("\033[31Invalid key name '{}', dude\033[0m", name);
                        return false;
                    }
                    return true;
                } else {
                    warnln("\033[31No active keyboard packet, dude\033[0m");
                    return false;
                }
            } });
        parser.add_option(Core::ArgsParser::Option {
            .requires_argument = true,
            .help_string = "Send a series of events to type out the given string",
            .long_name = "type",
            .accept_value = [&](auto* s) -> bool {
                if (has_started_keyboard_event) {
                    event_order.append(Key);
                    finished_key_events.append(resolve(pending_key_event, keymap()));
                    pending_key_event = {};
                }

                for (auto ch : StringView { s }) {
                    auto needed_flags_option = find_required_flags_for(ch, keymap());
                    if (!needed_flags_option.has_value())
                        return false;
                    auto needed_flags = needed_flags_option.value();
                    pending_key_event.key_str = String { &ch, 1 };
                    pending_key_event.flags = needed_flags | Is_Press;
                    event_order.append(Key);
                    finished_key_events.append(resolve(pending_key_event, keymap()));
                    pending_key_event.flags = needed_flags;
                    event_order.append(Key);
                    finished_key_events.append(resolve(pending_key_event, keymap()));
                }
                has_started_keyboard_event = false;
                pending_key_event = {};

                return true;
            } });
    }

    parser.parse(argc, argv);

    if (has_started_keyboard_event) {
        event_order.append(Key);
        finished_key_events.append(resolve(pending_key_event, keymap()));
    }
    if (has_started_mouse_packet) {
        event_order.append(Mouse);
        finished_mouse_packets.append(pending_mouse_packet);
    }

    size_t keyboard_index = 0;
    size_t mouse_index = 0;
    size_t index = 0;

    auto connection = WindowServerConnection::construct();
    RefPtr<Core::Timer> timer = Core::Timer::construct(delay, [&] {
        if (index >= event_order.size()) {
            timer->stop();
            loop.quit(0);
            return;
        }
        auto order = event_order[index++];
        switch (order) {
        case Key:
            connection->send_key_event(finished_key_events[keyboard_index++]);
            break;
        case Mouse:
            connection->send_mouse_event(finished_mouse_packets[mouse_index++]);
            break;
        }
    });
    return loop.exec();
}

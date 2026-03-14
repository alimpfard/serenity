/*
 * Copyright (c) 2024, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/LexicalPath.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/StringBuilder.h>
#include <Kernel/API/KeyCode.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/LocalServer.h>
#include <LibCore/Notifier.h>
#include <LibCore/ResourceImplementationFile.h>
#include <LibCore/SessionManagement.h>
#include <LibCore/Socket.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Font/FontDatabase.h>
#include <LibGfx/Painter.h>
#include <LibGfx/TextLayout.h>
#include <LibGfx/Palette.h>
#include <LibGfx/ShareableBitmap.h>
#include <LibGfx/SystemTheme.h>
#include <LibIPC/ConnectionFromClient.h>
#include <LibIPC/File.h>
#include <LibMain/Main.h>

#include <WindowServer/ScreenLayout.h>
#include <WindowClientEndpoint.h>
#include <WindowServerEndpoint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
extern "C" {
#include <xdg-shell-client-protocol.h>
}
#pragma GCC diagnostic pop

#include <linux/memfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

// =====================================================================
// XKB keysym to Serenity KeyCode mapping
// =====================================================================

static KeyCode xkb_keysym_to_serenity(xkb_keysym_t sym)
{
    // clang-format off
    switch (sym) {
    case XKB_KEY_Escape:       return Key_Escape;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key_Tab;
    case XKB_KEY_BackSpace:    return Key_Backspace;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:     return Key_Return;
    case XKB_KEY_Insert:
    case XKB_KEY_KP_Insert:    return Key_Insert;
    case XKB_KEY_Delete:
    case XKB_KEY_KP_Delete:    return Key_Delete;
    case XKB_KEY_Home:
    case XKB_KEY_KP_Home:      return Key_Home;
    case XKB_KEY_End:
    case XKB_KEY_KP_End:       return Key_End;
    case XKB_KEY_Left:
    case XKB_KEY_KP_Left:      return Key_Left;
    case XKB_KEY_Up:
    case XKB_KEY_KP_Up:        return Key_Up;
    case XKB_KEY_Right:
    case XKB_KEY_KP_Right:     return Key_Right;
    case XKB_KEY_Down:
    case XKB_KEY_KP_Down:      return Key_Down;
    case XKB_KEY_Page_Up:
    case XKB_KEY_KP_Page_Up:   return Key_PageUp;
    case XKB_KEY_Page_Down:
    case XKB_KEY_KP_Page_Down: return Key_PageDown;
    case XKB_KEY_Shift_L:      return Key_LeftShift;
    case XKB_KEY_Shift_R:      return Key_RightShift;
    case XKB_KEY_Control_L:    return Key_LeftControl;
    case XKB_KEY_Control_R:    return Key_RightControl;
    case XKB_KEY_Alt_L:        return Key_LeftAlt;
    case XKB_KEY_Alt_R:        return Key_RightAlt;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:      return Key_LeftSuper;
    case XKB_KEY_F1:           return Key_F1;
    case XKB_KEY_F2:           return Key_F2;
    case XKB_KEY_F3:           return Key_F3;
    case XKB_KEY_F4:           return Key_F4;
    case XKB_KEY_F5:           return Key_F5;
    case XKB_KEY_F6:           return Key_F6;
    case XKB_KEY_F7:           return Key_F7;
    case XKB_KEY_F8:           return Key_F8;
    case XKB_KEY_F9:           return Key_F9;
    case XKB_KEY_F10:          return Key_F10;
    case XKB_KEY_F11:          return Key_F11;
    case XKB_KEY_F12:          return Key_F12;
    case XKB_KEY_space:        return Key_Space;
    case XKB_KEY_a: case XKB_KEY_A: return Key_A;
    case XKB_KEY_b: case XKB_KEY_B: return Key_B;
    case XKB_KEY_c: case XKB_KEY_C: return Key_C;
    case XKB_KEY_d: case XKB_KEY_D: return Key_D;
    case XKB_KEY_e: case XKB_KEY_E: return Key_E;
    case XKB_KEY_f: case XKB_KEY_F: return Key_F;
    case XKB_KEY_g: case XKB_KEY_G: return Key_G;
    case XKB_KEY_h: case XKB_KEY_H: return Key_H;
    case XKB_KEY_i: case XKB_KEY_I: return Key_I;
    case XKB_KEY_j: case XKB_KEY_J: return Key_J;
    case XKB_KEY_k: case XKB_KEY_K: return Key_K;
    case XKB_KEY_l: case XKB_KEY_L: return Key_L;
    case XKB_KEY_m: case XKB_KEY_M: return Key_M;
    case XKB_KEY_n: case XKB_KEY_N: return Key_N;
    case XKB_KEY_o: case XKB_KEY_O: return Key_O;
    case XKB_KEY_p: case XKB_KEY_P: return Key_P;
    case XKB_KEY_q: case XKB_KEY_Q: return Key_Q;
    case XKB_KEY_r: case XKB_KEY_R: return Key_R;
    case XKB_KEY_s: case XKB_KEY_S: return Key_S;
    case XKB_KEY_t: case XKB_KEY_T: return Key_T;
    case XKB_KEY_u: case XKB_KEY_U: return Key_U;
    case XKB_KEY_v: case XKB_KEY_V: return Key_V;
    case XKB_KEY_w: case XKB_KEY_W: return Key_W;
    case XKB_KEY_x: case XKB_KEY_X: return Key_X;
    case XKB_KEY_y: case XKB_KEY_Y: return Key_Y;
    case XKB_KEY_z: case XKB_KEY_Z: return Key_Z;
    case XKB_KEY_0: case XKB_KEY_KP_0: return Key_0;
    case XKB_KEY_1: case XKB_KEY_KP_1: return Key_1;
    case XKB_KEY_2: case XKB_KEY_KP_2: return Key_2;
    case XKB_KEY_3: case XKB_KEY_KP_3: return Key_3;
    case XKB_KEY_4: case XKB_KEY_KP_4: return Key_4;
    case XKB_KEY_5: case XKB_KEY_KP_5: return Key_5;
    case XKB_KEY_6: case XKB_KEY_KP_6: return Key_6;
    case XKB_KEY_7: case XKB_KEY_KP_7: return Key_7;
    case XKB_KEY_8: case XKB_KEY_KP_8: return Key_8;
    case XKB_KEY_9: case XKB_KEY_KP_9: return Key_9;
    case XKB_KEY_minus:
    case XKB_KEY_KP_Subtract:  return Key_Minus;
    case XKB_KEY_plus:
    case XKB_KEY_KP_Add:       return Key_Plus;
    case XKB_KEY_equal:        return Key_Equal;
    case XKB_KEY_period:
    case XKB_KEY_KP_Decimal:   return Key_Period;
    case XKB_KEY_comma:        return Key_Comma;
    case XKB_KEY_slash:
    case XKB_KEY_KP_Divide:    return Key_Slash;
    case XKB_KEY_asterisk:
    case XKB_KEY_KP_Multiply:  return Key_Asterisk;
    case XKB_KEY_semicolon:    return Key_Semicolon;
    case XKB_KEY_apostrophe:   return Key_Apostrophe;
    case XKB_KEY_grave:        return Key_Backtick;
    case XKB_KEY_bracketleft:  return Key_LeftBracket;
    case XKB_KEY_bracketright: return Key_RightBracket;
    case XKB_KEY_backslash:    return Key_Backslash;
    case XKB_KEY_underscore:   return Key_Underscore;
    case XKB_KEY_exclam:       return Key_ExclamationPoint;
    case XKB_KEY_at:           return Key_AtSign;
    case XKB_KEY_numbersign:   return Key_Hashtag;
    case XKB_KEY_dollar:       return Key_Dollar;
    case XKB_KEY_percent:      return Key_Percent;
    case XKB_KEY_asciicircum:  return Key_Circumflex;
    case XKB_KEY_ampersand:    return Key_Ampersand;
    case XKB_KEY_parenleft:    return Key_LeftParen;
    case XKB_KEY_parenright:   return Key_RightParen;
    case XKB_KEY_braceleft:    return Key_LeftBrace;
    case XKB_KEY_braceright:   return Key_RightBrace;
    case XKB_KEY_bar:          return Key_Pipe;
    case XKB_KEY_asciitilde:   return Key_Tilde;
    case XKB_KEY_colon:        return Key_Colon;
    case XKB_KEY_quotedbl:     return Key_DoubleQuote;
    case XKB_KEY_less:         return Key_LessThan;
    case XKB_KEY_greater:      return Key_GreaterThan;
    case XKB_KEY_question:     return Key_QuestionMark;
    default:                   return Key_Invalid;
    }
    // clang-format on
}

// =====================================================================
// Wayland global state
// =====================================================================

struct WaylandGlobals {
    wl_display* display { nullptr };
    wl_registry* registry { nullptr };
    wl_compositor* compositor { nullptr };
    wl_shm* shm { nullptr };
    xdg_wm_base* wm_base { nullptr };
    wl_seat* seat { nullptr };
    wl_keyboard* keyboard { nullptr };
    wl_pointer* pointer { nullptr };

    // XKB keyboard state
    struct xkb_context* xkb_ctx { nullptr };
    struct xkb_keymap* kb_keymap { nullptr };
    struct xkb_state* kb_state { nullptr };

    // Input focus tracking
    wl_surface* keyboard_focus_surface { nullptr };
    wl_surface* pointer_focus_surface { nullptr };
    Gfx::IntPoint pointer_position;
    uint32_t pointer_button_state { 0 };
    uint32_t keyboard_serial { 0 };
    uint32_t pointer_serial { 0 };
};

static WaylandGlobals* s_wayland { nullptr };

// =====================================================================
// Menu data structures
// =====================================================================

static constexpr int MENUBAR_HEIGHT = 20;
static constexpr int MENU_ITEM_PADDING = 8;

struct MenuItem {
    i32 identifier { -1 };
    ByteString text;
    bool enabled { true };
    bool visible { true };
    bool checkable { false };
    bool checked { false };
    bool is_separator { false };
    ByteString shortcut;
    i32 submenu_id { -1 };
};

struct Menu {
    i32 menu_id { -1 };
    String name;
    Vector<MenuItem> items;
    int menubar_rect_x { 0 };
    int menubar_rect_width { 0 };
};

// =====================================================================
// Per-window Wayland state
// =====================================================================

struct WaylandWindowState {
    int window_id { -1 };
    int client_id { -1 };
    wl_surface* surface { nullptr };
    struct xdg_surface* xdg_surf { nullptr };
    xdg_toplevel* toplevel { nullptr };

    RefPtr<Gfx::Bitmap> backing_store;
    Gfx::IntSize backing_store_visible_size;
    bool has_alpha { false };
    ByteString title;
    Gfx::IntRect rect;
    bool configured { false };

    // Menubar: ordered list of menu_ids for this window
    Vector<i32> menubar_menu_ids;
    bool has_menubar() const { return !menubar_menu_ids.is_empty(); }
    int menubar_height() const { return has_menubar() ? MENUBAR_HEIGHT : 0; }

    // Wayland SHM double-buffer state
    wl_buffer* wl_buf { nullptr };       // front buffer
    wl_buffer* wl_buf_back { nullptr };  // back buffer
    void* shm_data { nullptr };
    int shm_fd { -1 };
    size_t shm_size { 0 };
    bool front_buffer_busy { false };
    bool back_buffer_busy { false };
    bool using_front { true };

    void destroy_wayland_objects()
    {
        if (wl_buf) {
            wl_buffer_destroy(wl_buf);
            wl_buf = nullptr;
        }
        if (wl_buf_back) {
            wl_buffer_destroy(wl_buf_back);
            wl_buf_back = nullptr;
        }
        if (shm_data && shm_size > 0) {
            munmap(shm_data, shm_size);
            shm_data = nullptr;
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }
        if (toplevel) {
            xdg_toplevel_destroy(toplevel);
            toplevel = nullptr;
        }
        if (xdg_surf) {
            xdg_surface_destroy(xdg_surf);
            xdg_surf = nullptr;
        }
        if (surface) {
            wl_surface_destroy(surface);
            surface = nullptr;
        }
    }
};

// Global window tracking (wl_surface → window state)
static HashMap<wl_surface*, WaylandWindowState*> s_surface_to_window;

// =====================================================================
// Forward declarations
// =====================================================================

class ClientConnection;
static HashMap<int, NonnullRefPtr<ClientConnection>>* s_connections;
static Core::AnonymousBuffer s_theme_buffer;
static int s_next_client_id { 0 };
static Gfx::IntRect s_screen_rect { 0, 0, 1920, 1080 };

// =====================================================================
// ClientConnection: implements the WindowServer IPC protocol
// =====================================================================

class ClientConnection final
    : public IPC::ConnectionFromClient<WindowClientEndpoint, WindowServerEndpoint> {
    C_OBJECT(ClientConnection)

public:
    ~ClientConnection() override
    {
        for (auto& [_, win] : m_windows) {
            s_surface_to_window.remove(win.surface);
            win.destroy_wayland_objects();
        }
        m_windows.clear();
    }

    WaylandWindowState* find_window_by_surface(wl_surface* surface)
    {
        for (auto& [_, win] : m_windows) {
            if (win.surface == surface)
                return &win;
        }
        return nullptr;
    }

    WaylandWindowState* window_from_id(i32 id)
    {
        auto it = m_windows.find(id);
        if (it == m_windows.end())
            return nullptr;
        return &it->value;
    }

    int connection_client_id() const { return client_id(); }

private:
    explicit ClientConnection(NonnullOwnPtr<Core::LocalSocket> socket, int cid)
        : IPC::ConnectionFromClient<WindowClientEndpoint, WindowServerEndpoint>(*this, move(socket), cid)
    {
        if (!s_connections)
            s_connections = new HashMap<int, NonnullRefPtr<ClientConnection>>;
        s_connections->set(cid, *this);

        // Send greeting
        Vector<Gfx::IntRect> screen_rects;
        screen_rects.append(s_screen_rect);
        Vector<bool> effects;
        for (int i = 0; i < 6; i++)
            effects.append(false);

        async_fast_greet(
            screen_rects,
            0, // main_screen_index
            1, // workspace_rows
            1, // workspace_columns
            s_theme_buffer,
            Gfx::FontDatabase::default_font_query(),
            Gfx::FontDatabase::fixed_width_font_query(),
            ByteString("Liberation Sans 10 700 0"sv),
            effects,
            cid);
    }

    void die() override
    {
        deferred_invoke([this] {
            if (s_connections)
                s_connections->remove(client_id());
        });
    }

    void may_have_become_unresponsive() override { }
    void did_become_responsive() override { }

    // ---- Window creation/destruction ----

    void create_window(i32 window_id, i32, Gfx::IntRect const& rect, bool, bool has_alpha,
        bool, bool, bool resizable, bool, bool frameless, bool, float,
        Gfx::IntSize, Gfx::IntSize, Gfx::IntSize,
        Optional<Gfx::IntSize> const&, i32, i32, ByteString const& title,
        i32, Gfx::IntRect const&) override
    {
        (void)frameless;
        auto& wl = *s_wayland;

        WaylandWindowState win;
        win.window_id = window_id;
        win.client_id = client_id();
        win.has_alpha = has_alpha;
        win.title = title;
        win.rect = rect;

        // Create Wayland surface
        win.surface = wl_compositor_create_surface(wl.compositor);
        win.xdg_surf = xdg_wm_base_get_xdg_surface(wl.wm_base, win.surface);
        win.toplevel = xdg_surface_get_toplevel(win.xdg_surf);

        if (!title.is_empty())
            xdg_toplevel_set_title(win.toplevel, title.characters());

        xdg_toplevel_set_app_id(win.toplevel, "serenity");

        if (resizable && rect.width() > 0 && rect.height() > 0) {
            xdg_toplevel_set_min_size(win.toplevel, 100, 100);
        }

        // XDG surface configure callback - ack and flush pending content
        static struct xdg_surface_listener const xdg_surface_listener = {
            .configure = [](void*, xdg_surface* xsurf, uint32_t serial) {
                xdg_surface_ack_configure(xsurf, serial);

                // After acking configure, flush any pending backing store
                for (auto& [_, conn] : *s_connections) {
                    for (auto& [__, win] : conn->m_windows) {
                        if (win.xdg_surf == xsurf) {
                            win.configured = true;
                            if (win.backing_store) {
                                conn->flush_window_to_wayland(win);
                            } else {
                                // No backing store yet, ask client to paint
                                conn->async_paint(win.window_id, win.rect.size(), Vector<Gfx::IntRect> { win.rect });
                            }
                            return;
                        }
                    }
                }
            },
        };
        xdg_surface_add_listener(win.xdg_surf, &xdg_surface_listener, nullptr);

        // XDG toplevel configure callback
        static struct xdg_toplevel_listener const toplevel_listener = {
            .configure = [](void*, xdg_toplevel* tl, int32_t width, int32_t height, wl_array*) {
                if (width <= 0 || height <= 0)
                    return;

                // Find the window for this toplevel
                for (auto& [_, conn] : *s_connections) {
                    for (auto& [__, win] : conn->m_windows) {
                        if (win.toplevel == tl) {
                            auto old_rect = win.rect;
                            win.rect.set_width(width);
                            win.rect.set_height(height);
                            if (old_rect.size() != win.rect.size()) {
                                conn->async_window_resized(win.window_id, win.rect);
                            }
                            return;
                        }
                    }
                }
            },
            .close = [](void*, xdg_toplevel* tl) {
                for (auto& [_, conn] : *s_connections) {
                    for (auto& [__, win] : conn->m_windows) {
                        if (win.toplevel == tl) {
                            conn->async_window_close_request(win.window_id);
                            return;
                        }
                    }
                }
            },
            .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
            .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
        };
        xdg_toplevel_add_listener(win.toplevel, &toplevel_listener, nullptr);

        wl_surface_commit(win.surface);

        s_surface_to_window.set(win.surface, nullptr); // Placeholder, updated below
        m_windows.set(window_id, move(win));

        // Update the surface→window mapping with the actual pointer
        auto* win_ptr = &m_windows.find(window_id)->value;
        s_surface_to_window.set(win_ptr->surface, win_ptr);

        async_window_activated(window_id);
    }

    Messages::WindowServer::DestroyWindowResponse destroy_window(i32 window_id) override
    {
        Vector<i32> destroyed;
        auto it = m_windows.find(window_id);
        if (it != m_windows.end()) {
            s_surface_to_window.remove(it->value.surface);
            it->value.destroy_wayland_objects();
            m_windows.remove(it);
            destroyed.append(window_id);
        }
        return destroyed;
    }

    // ---- Window properties ----

    void set_window_title(i32 window_id, ByteString const& title) override
    {
        if (auto* win = window_from_id(window_id)) {
            win->title = title;
            if (win->toplevel)
                xdg_toplevel_set_title(win->toplevel, title.characters());
        }
    }

    Messages::WindowServer::GetWindowTitleResponse get_window_title(i32 window_id) override
    {
        if (auto* win = window_from_id(window_id))
            return win->title;
        return ByteString {};
    }

    Messages::WindowServer::SetWindowRectResponse set_window_rect(i32 window_id, Gfx::IntRect const& rect) override
    {
        if (auto* win = window_from_id(window_id)) {
            win->rect = rect;
            // Wayland doesn't allow client-driven positioning, but we can hint size
        }
        return rect;
    }

    Messages::WindowServer::GetWindowRectResponse get_window_rect(i32 window_id) override
    {
        if (auto* win = window_from_id(window_id))
            return win->rect;
        return Gfx::IntRect {};
    }

    Messages::WindowServer::GetWindowFloatingRectResponse get_window_floating_rect(i32 window_id) override
    {
        return get_window_rect(window_id).rect();
    }

    void set_window_minimum_size(i32 window_id, Gfx::IntSize size) override
    {
        if (auto* win = window_from_id(window_id)) {
            if (win->toplevel)
                xdg_toplevel_set_min_size(win->toplevel, size.width(), size.height());
        }
    }

    Messages::WindowServer::GetWindowMinimumSizeResponse get_window_minimum_size(i32) override
    {
        return Gfx::IntSize { 0, 0 };
    }

    // ---- Backing store / painting ----

    void set_window_backing_store(i32 window_id, i32, i32, IPC::File const& anon_file,
        i32, bool has_alpha, Gfx::IntSize size, Gfx::IntSize visible_size,
        bool flush_immediately) override
    {
        auto* win = window_from_id(window_id);
        if (!win)
            return;

        auto anon_buffer_or_error = Core::AnonymousBuffer::create_from_anon_fd(
            anon_file.take_fd(), static_cast<size_t>(size.width()) * size.height() * 4);
        if (anon_buffer_or_error.is_error())
            return;

        auto format = has_alpha ? Gfx::BitmapFormat::BGRA8888 : Gfx::BitmapFormat::BGRx8888;
        auto bitmap_or_error = Gfx::Bitmap::create_with_anonymous_buffer(
            format, anon_buffer_or_error.release_value(), size, 1);
        if (bitmap_or_error.is_error())
            return;

        win->backing_store = bitmap_or_error.release_value();
        win->backing_store_visible_size = visible_size;
        win->has_alpha = has_alpha;

        if (flush_immediately)
            flush_window_to_wayland(*win);
    }

    void invalidate_rect(i32 window_id, Vector<Gfx::IntRect> const& rects, bool) override
    {
        if (auto* win = window_from_id(window_id))
            async_paint(window_id, win->rect.size(), rects);
    }

    void did_finish_painting(i32 window_id, Vector<Gfx::IntRect> const&) override
    {
        if (auto* win = window_from_id(window_id))
            flush_window_to_wayland(*win);
    }

public:
    bool is_popup_surface(wl_surface* s) const { return m_popup_surface == s && m_open_menu_id != -1; }

    // ---- Menu interaction ----

    void handle_menubar_click(WaylandWindowState* win, Gfx::IntPoint pos)
    {
        for (auto menu_id : win->menubar_menu_ids) {
            auto it = m_menus.find(menu_id);
            if (it == m_menus.end())
                continue;
            auto& menu = it->value;
            if (pos.x() >= menu.menubar_rect_x
                && pos.x() < menu.menubar_rect_x + menu.menubar_rect_width) {
                if (m_open_menu_id == menu_id) {
                    // Toggle off
                    dismiss_open_menu();
                } else {
                    open_menu_dropdown(win, menu);
                }
                return;
            }
        }
    }

    void open_menu_dropdown(WaylandWindowState* win, Menu& menu)
    {
        m_open_menu_id = menu.menu_id;
        m_open_menu_window_id = win->window_id;

        // Create a popup surface for the dropdown
        if (!m_popup_surface) {
            m_popup_surface = wl_compositor_create_surface(s_wayland->compositor);
        }

        // Render menu items to a bitmap
        auto& font = Gfx::FontDatabase::default_font();
        int item_height = font.pixel_size_rounded_up() + 6;
        int visible_items = 0;
        int max_text_width = 100;
        for (auto& item : menu.items) {
            if (!item.visible) continue;
            visible_items++;
            if (!item.is_separator) {
                int w = font.width(item.text) + 40;
                if (!item.shortcut.is_empty())
                    w += font.width(item.shortcut) + 20;
                max_text_width = max(max_text_width, w);
            }
        }
        int popup_w = max_text_width;
        int popup_h = visible_items * item_height + 4;

        auto popup_bitmap = MUST(Gfx::Bitmap::create(Gfx::BitmapFormat::BGRx8888, { popup_w, popup_h }));
        Gfx::Painter painter(*popup_bitmap);
        painter.fill_rect(popup_bitmap->rect(), Gfx::Color(240, 240, 240));
        painter.draw_rect(popup_bitmap->rect(), Gfx::Color(180, 180, 180));

        int y = 2;
        m_popup_item_rects.clear();
        for (auto& item : menu.items) {
            if (!item.visible) continue;
            if (item.is_separator) {
                painter.draw_line({ 4, y + item_height / 2 }, { popup_w - 5, y + item_height / 2 }, Gfx::Color(200, 200, 200));
                m_popup_item_rects.append({ .identifier = -1, .rect = { 0, y, popup_w, item_height } });
            } else {
                auto text_color = item.enabled ? Gfx::Color::Black : Gfx::Color(160, 160, 160);
                auto display_text = item.text.replace("&"sv, ""sv, ReplaceMode::All);
                Gfx::IntRect text_rect { 20, y, popup_w - 40, item_height };
                painter.draw_text(text_rect, display_text.view(), font, Gfx::TextAlignment::CenterLeft, text_color);
                if (!item.shortcut.is_empty()) {
                    Gfx::IntRect sc_rect { 20, y, popup_w - 24, item_height };
                    painter.draw_text(sc_rect, item.shortcut.view(), font, Gfx::TextAlignment::CenterRight, Gfx::Color(120, 120, 120));
                }
                if (item.checkable && item.checked) {
                    Gfx::IntRect ck_rect { 2, y, 16, item_height };
                    painter.draw_text(ck_rect, "\xE2\x9C\x93"sv, font, Gfx::TextAlignment::Center, text_color);
                }
                m_popup_item_rects.append({ .identifier = item.identifier, .rect = { 0, y, popup_w, item_height } });
            }
            y += item_height;
        }

        // Create SHM buffer for popup
        int popup_stride = popup_w * 4;
        size_t popup_size = static_cast<size_t>(popup_stride) * popup_h;
        int popup_fd = memfd_create("serenity-popup", MFD_CLOEXEC);
        ftruncate(popup_fd, static_cast<off_t>(popup_size));
        void* popup_data = mmap(nullptr, popup_size, PROT_READ | PROT_WRITE, MAP_SHARED, popup_fd, 0);
        memcpy(popup_data, popup_bitmap->scanline_u8(0), popup_size);

        auto* pool = wl_shm_create_pool(s_wayland->shm, popup_fd, static_cast<int32_t>(popup_size));
        if (m_popup_buffer)
            wl_buffer_destroy(m_popup_buffer);
        m_popup_buffer = wl_shm_pool_create_buffer(pool, 0, popup_w, popup_h, popup_stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        munmap(popup_data, popup_size);
        close(popup_fd);

        // Position popup using xdg_popup
        if (m_popup_xdg_surface)
            xdg_surface_destroy(m_popup_xdg_surface);
        if (m_popup_xdg_popup)
            xdg_popup_destroy(m_popup_xdg_popup);

        m_popup_xdg_surface = xdg_wm_base_get_xdg_surface(s_wayland->wm_base, m_popup_surface);
        auto* positioner = xdg_wm_base_create_positioner(s_wayland->wm_base);
        xdg_positioner_set_size(positioner, popup_w, popup_h);
        xdg_positioner_set_anchor_rect(positioner, menu.menubar_rect_x, 0, menu.menubar_rect_width, win->menubar_height());
        xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
        xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

        m_popup_xdg_popup = xdg_surface_get_popup(m_popup_xdg_surface, win->xdg_surf, positioner);
        xdg_positioner_destroy(positioner);

        static struct xdg_surface_listener const popup_surface_listener = {
            .configure = [](void*, xdg_surface* surf, uint32_t serial) {
                xdg_surface_ack_configure(surf, serial);
            },
        };
        xdg_surface_add_listener(m_popup_xdg_surface, &popup_surface_listener, nullptr);

        static struct xdg_popup_listener const popup_listener = {
            .configure = [](void*, xdg_popup*, int32_t, int32_t, int32_t, int32_t) {},
            .popup_done = [](void*, xdg_popup*) {
                // Popup was dismissed by compositor
                if (s_connections) {
                    for (auto& [_, conn] : *s_connections) {
                        conn->dismiss_open_menu();
                    }
                }
            },
            .repositioned = [](void*, xdg_popup*, uint32_t) {},
        };
        xdg_popup_add_listener(m_popup_xdg_popup, &popup_listener, nullptr);

        wl_surface_commit(m_popup_surface);
        wl_display_roundtrip(s_wayland->display);

        wl_surface_attach(m_popup_surface, m_popup_buffer, 0, 0);
        wl_surface_damage_buffer(m_popup_surface, 0, 0, popup_w, popup_h);
        wl_surface_commit(m_popup_surface);
        wl_display_flush(s_wayland->display);

        // Redraw menubar to highlight active item
        flush_window_to_wayland(*win);
    }

    void dismiss_open_menu()
    {
        if (m_open_menu_id == -1)
            return;
        m_open_menu_id = -1;

        if (m_popup_xdg_popup) {
            xdg_popup_destroy(m_popup_xdg_popup);
            m_popup_xdg_popup = nullptr;
        }
        if (m_popup_xdg_surface) {
            xdg_surface_destroy(m_popup_xdg_surface);
            m_popup_xdg_surface = nullptr;
        }
        if (m_popup_surface) {
            wl_surface_attach(m_popup_surface, nullptr, 0, 0);
            wl_surface_commit(m_popup_surface);
        }

        // Redraw menubar
        if (auto* win = window_from_id(m_open_menu_window_id))
            flush_window_to_wayland(*win);
        m_open_menu_window_id = -1;
    }

    void handle_popup_click(Gfx::IntPoint pos)
    {
        for (auto& entry : m_popup_item_rects) {
            if (entry.identifier >= 0 && entry.rect.contains(pos)) {
                async_menu_item_activated(m_open_menu_id, static_cast<u32>(entry.identifier));
                dismiss_open_menu();
                return;
            }
        }
    }

    // ---- Flush backing store to Wayland surface ----

    void render_menubar(Gfx::Painter& painter, WaylandWindowState& win, int width)
    {
        if (!win.has_menubar())
            return;

        int bar_h = win.menubar_height();
        // Background
        painter.fill_rect({ 0, 0, width, bar_h }, Gfx::Color(220, 220, 220));
        // Bottom border
        painter.draw_line({ 0, bar_h - 1 }, { width - 1, bar_h - 1 }, Gfx::Color(180, 180, 180));

        auto& font = Gfx::FontDatabase::default_font();
        int x = 4;
        for (auto menu_id : win.menubar_menu_ids) {
            auto it = m_menus.find(menu_id);
            if (it == m_menus.end())
                continue;
            auto& menu = it->value;
            auto display_name = menu.name.bytes_as_string_view().replace("&"sv, ""sv, ReplaceMode::All);
            int text_width = font.width(display_name) + MENU_ITEM_PADDING * 2;

            menu.menubar_rect_x = x;
            menu.menubar_rect_width = text_width;

            bool is_open = (m_open_menu_id == menu_id);
            Gfx::IntRect title_rect { x, 0, text_width, bar_h - 1 };
            if (is_open)
                painter.fill_rect(title_rect, Gfx::Color(180, 200, 240));

            painter.draw_text(title_rect, display_name.view(), font,
                Gfx::TextAlignment::Center, Gfx::Color::Black);
            x += text_width;
        }
    }

    void flush_window_to_wayland(WaylandWindowState& win)
    {
        if (!win.backing_store || !win.surface || !win.configured)
            return;

        auto& wl = *s_wayland;
        auto& bitmap = *win.backing_store;
        int width = bitmap.width();
        int client_height = bitmap.height();
        int mb_h = win.menubar_height();
        int total_height = client_height + mb_h;
        int stride = width * 4;
        size_t one_buffer_size = static_cast<size_t>(stride) * total_height;
        // Double-buffered: pool holds 2 frames
        size_t required_pool_size = one_buffer_size * 2;

        // Reallocate SHM pool if the size changed
        if (win.shm_size < required_pool_size || !win.shm_data) {
            // Destroy old buffers
            if (win.wl_buf) {
                wl_buffer_destroy(win.wl_buf);
                win.wl_buf = nullptr;
            }
            if (win.wl_buf_back) {
                wl_buffer_destroy(win.wl_buf_back);
                win.wl_buf_back = nullptr;
            }
            if (win.shm_data && win.shm_size > 0) {
                munmap(win.shm_data, win.shm_size);
                win.shm_data = nullptr;
            }
            if (win.shm_fd >= 0) {
                close(win.shm_fd);
                win.shm_fd = -1;
            }

            win.shm_fd = memfd_create("serenity-wl-shm", MFD_CLOEXEC);
            if (win.shm_fd < 0)
                return;

            win.shm_size = required_pool_size;
            if (ftruncate(win.shm_fd, static_cast<off_t>(win.shm_size)) < 0) {
                close(win.shm_fd);
                win.shm_fd = -1;
                return;
            }

            win.shm_data = mmap(nullptr, win.shm_size, PROT_READ | PROT_WRITE,
                MAP_SHARED, win.shm_fd, 0);
            if (win.shm_data == MAP_FAILED) {
                win.shm_data = nullptr;
                close(win.shm_fd);
                win.shm_fd = -1;
                return;
            }

            // Create pool and two buffers at different offsets
            auto format = win.has_alpha ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888;
            auto* pool = wl_shm_create_pool(wl.shm, win.shm_fd, static_cast<int32_t>(win.shm_size));
            win.wl_buf = wl_shm_pool_create_buffer(pool, 0, width, total_height, stride, format);
            win.wl_buf_back = wl_shm_pool_create_buffer(pool, static_cast<int32_t>(one_buffer_size), width, total_height, stride, format);
            wl_shm_pool_destroy(pool);

            static struct wl_buffer_listener const front_buf_listener = {
                .release = [](void* data, wl_buffer*) {
                    static_cast<WaylandWindowState*>(data)->front_buffer_busy = false;
                },
            };
            static struct wl_buffer_listener const back_buf_listener = {
                .release = [](void* data, wl_buffer*) {
                    static_cast<WaylandWindowState*>(data)->back_buffer_busy = false;
                },
            };
            wl_buffer_add_listener(win.wl_buf, &front_buf_listener, &win);
            wl_buffer_add_listener(win.wl_buf_back, &back_buf_listener, &win);

            win.front_buffer_busy = false;
            win.back_buffer_busy = false;
            win.using_front = true;
        }

        // Pick the non-busy buffer, preferring to alternate
        wl_buffer* buf_to_use;
        void* data_dest;
        if (win.using_front && !win.back_buffer_busy) {
            buf_to_use = win.wl_buf_back;
            data_dest = static_cast<u8*>(win.shm_data) + one_buffer_size;
            win.using_front = false;
        } else if (!win.using_front && !win.front_buffer_busy) {
            buf_to_use = win.wl_buf;
            data_dest = win.shm_data;
            win.using_front = true;
        } else if (!win.front_buffer_busy) {
            buf_to_use = win.wl_buf;
            data_dest = win.shm_data;
            win.using_front = true;
        } else if (!win.back_buffer_busy) {
            buf_to_use = win.wl_buf_back;
            data_dest = static_cast<u8*>(win.shm_data) + one_buffer_size;
            win.using_front = false;
        } else {
            // Both buffers busy, skip this frame
            return;
        }

        // Render menubar + client content into the chosen buffer
        {
            auto dest_bitmap = MUST(Gfx::Bitmap::create_wrapper(
                Gfx::BitmapFormat::BGRx8888, { width, total_height }, 1,
                static_cast<size_t>(stride), static_cast<u8*>(data_dest)));
            Gfx::Painter painter(*dest_bitmap);

            // Draw menubar
            if (mb_h > 0)
                render_menubar(painter, win, width);

            // Draw client content below menubar
            painter.blit({ 0, mb_h }, bitmap, bitmap.rect());
        }

        wl_surface_attach(win.surface, buf_to_use, 0, 0);
        wl_surface_damage_buffer(win.surface, 0, 0, width, total_height);
        wl_surface_commit(win.surface);
        wl_display_flush(s_wayland->display);

        if (buf_to_use == win.wl_buf)
            win.front_buffer_busy = true;
        else
            win.back_buffer_busy = true;
    }

private:
    // ---- Menu implementation ----

    void create_menu(i32 menu_id, String const& name, i32) override
    {
        Menu menu;
        menu.menu_id = menu_id;
        menu.name = name;
        m_menus.set(menu_id, move(menu));
    }

    void set_menu_name(i32 menu_id, String const& name) override
    {
        if (auto it = m_menus.find(menu_id); it != m_menus.end())
            it->value.name = name;
    }

    void set_menu_minimum_width(i32, i32) override { }

    void destroy_menu(i32 menu_id) override
    {
        m_menus.remove(menu_id);
    }

    void add_menu(i32 window_id, i32 menu_id) override
    {
        if (auto* win = window_from_id(window_id)) {
            win->menubar_menu_ids.append(menu_id);
            // Force a redraw to show the menubar
            if (win->backing_store)
                flush_window_to_wayland(*win);
        }
    }

    void add_menu_item(i32 menu_id, i32 identifier, i32 submenu_id, ByteString const& text,
        bool enabled, bool visible, bool checkable, bool checked, bool,
        ByteString const& shortcut, Gfx::ShareableBitmap const&, bool) override
    {
        if (auto it = m_menus.find(menu_id); it != m_menus.end()) {
            MenuItem item;
            item.identifier = identifier;
            item.text = text;
            item.enabled = enabled;
            item.visible = visible;
            item.checkable = checkable;
            item.checked = checked;
            item.shortcut = shortcut;
            item.submenu_id = submenu_id;
            it->value.items.append(move(item));
        }
    }

    void add_menu_separator(i32 menu_id) override
    {
        if (auto it = m_menus.find(menu_id); it != m_menus.end()) {
            MenuItem sep;
            sep.is_separator = true;
            it->value.items.append(move(sep));
        }
    }

    void update_menu_item(i32 menu_id, i32 identifier, i32 submenu_id, ByteString const& text,
        bool enabled, bool visible, bool checkable, bool checked, bool,
        ByteString const& shortcut, Gfx::ShareableBitmap const&) override
    {
        if (auto it = m_menus.find(menu_id); it != m_menus.end()) {
            for (auto& item : it->value.items) {
                if (item.identifier == identifier) {
                    item.text = text;
                    item.enabled = enabled;
                    item.visible = visible;
                    item.checkable = checkable;
                    item.checked = checked;
                    item.shortcut = shortcut;
                    item.submenu_id = submenu_id;
                    break;
                }
            }
        }
    }

    void remove_menu_item(i32 menu_id, i32 identifier) override
    {
        if (auto it = m_menus.find(menu_id); it != m_menus.end()) {
            it->value.items.remove_all_matching([&](auto& item) {
                return item.identifier == identifier;
            });
        }
    }

    void flash_menubar_menu(i32, i32) override { }
    void popup_menu(i32, Gfx::IntPoint, Gfx::IntRect const&) override { }
    void dismiss_menu(i32) override { m_open_menu_id = -1; }

    // ---- Window state stubs ----

    Messages::WindowServer::IsMaximizedResponse is_maximized(i32) override { return false; }
    void set_maximized(i32, bool) override { }
    Messages::WindowServer::IsMinimizedResponse is_minimized(i32) override { return false; }
    void set_minimized(i32, bool) override { }
    void start_window_resize(i32, i32) override { }
    Messages::WindowServer::GetAppletRectOnScreenResponse get_applet_rect_on_screen(i32) override { return Gfx::IntRect {}; }
    void set_global_mouse_tracking(bool) override { }
    void set_window_has_alpha_channel(i32, bool) override { }
    void set_window_alpha_hit_threshold(i32, float) override { }
    void move_window_to_front(i32) override { }
    void set_fullscreen(i32, bool) override { }
    void set_frameless(i32, bool) override { }
    void set_forced_shadow(i32, bool) override { }
    void set_window_cursor(i32, i32) override { }
    void set_window_custom_cursor(i32, Gfx::ShareableBitmap const&) override { }
    void set_window_icon_bitmap(i32, Gfx::ShareableBitmap const&) override { }
    void set_window_modified(i32, bool) override { }
    Messages::WindowServer::IsWindowModifiedResponse is_window_modified(i32) override { return false; }
    void set_window_progress(i32, Optional<i32> const&) override { }
    void set_window_base_size_and_size_increment(i32, Gfx::IntSize, Gfx::IntSize) override { }
    void set_window_resize_aspect_ratio(i32, Optional<Gfx::IntSize> const&) override { }
    void set_always_on_top(i32, bool) override { }

    // ---- Wallpaper/theme/system stubs ----

    Messages::WindowServer::SetWallpaperResponse set_wallpaper(Gfx::ShareableBitmap const&) override { return true; }
    void set_background_color(ByteString const&) override { }
    void set_wallpaper_mode(ByteString const&) override { }
    Messages::WindowServer::GetWallpaperResponse get_wallpaper() override { return Gfx::ShareableBitmap {}; }

    Messages::WindowServer::SetScreenLayoutResponse set_screen_layout(WindowServer::ScreenLayout const&, bool) override
    {
        return { true, ByteString {} };
    }
    Messages::WindowServer::GetScreenLayoutResponse get_screen_layout() override
    {
        WindowServer::ScreenLayout layout;
        layout.screens.append({
            .mode = WindowServer::ScreenLayout::Screen::Mode::Virtual,
            .device = {},
            .location = { 0, 0 },
            .resolution = s_screen_rect.size(),
            .scale_factor = 1,
        });
        layout.main_screen_index = 0;
        return layout;
    }
    Messages::WindowServer::SaveScreenLayoutResponse save_screen_layout() override { return { true, ByteString {} }; }

    Messages::WindowServer::ApplyWorkspaceSettingsResponse apply_workspace_settings(u32, u32, bool) override { return true; }
    Messages::WindowServer::GetWorkspaceSettingsResponse get_workspace_settings() override { return { 1, 1, 16, 16 }; }
    void show_screen_numbers(bool) override { }

    Messages::WindowServer::SetSystemThemeResponse set_system_theme(ByteString const&, ByteString const&, bool, Optional<ByteString> const&) override { return true; }
    Messages::WindowServer::GetSystemThemeResponse get_system_theme() override { return ByteString("Default"sv); }
    void refresh_system_theme() override { }
    Messages::WindowServer::SetSystemThemeOverrideResponse set_system_theme_override(Core::AnonymousBuffer const&) override { return true; }
    Messages::WindowServer::GetSystemThemeOverrideResponse get_system_theme_override() override { return OptionalNone(); }
    void clear_system_theme_override() override { }
    Messages::WindowServer::IsSystemThemeOverriddenResponse is_system_theme_overridden() override { return false; }
    Messages::WindowServer::GetPreferredColorSchemeResponse get_preferred_color_scheme() override { return OptionalNone(); }

    void apply_cursor_theme(ByteString const&) override { }
    Messages::WindowServer::GetCursorThemeResponse get_cursor_theme() override { return ByteString("Default"sv); }
    void set_cursor_highlight_radius(int) override { }
    Messages::WindowServer::GetCursorHighlightRadiusResponse get_cursor_highlight_radius() override { return 0; }
    void set_cursor_highlight_color(Gfx::Color) override { }
    Messages::WindowServer::GetCursorHighlightColorResponse get_cursor_highlight_color() override { return Gfx::Color {}; }

    Messages::WindowServer::SetSystemFontsResponse set_system_fonts(ByteString const&, ByteString const&, ByteString const&) override { return true; }
    void set_system_effects(Vector<bool> const&, u8, u8) override { }

    void enable_display_link() override { }
    void disable_display_link() override { }
    void pong() override { }

    void set_global_cursor_position(Gfx::IntPoint) override { }
    Messages::WindowServer::GetGlobalCursorPositionResponse get_global_cursor_position() override { return s_wayland->pointer_position; }

    void set_mouse_acceleration(float) override { }
    Messages::WindowServer::GetMouseAccelerationResponse get_mouse_acceleration() override { return 1.0f; }
    void set_scroll_step_size(u32) override { }
    Messages::WindowServer::GetScrollStepSizeResponse get_scroll_step_size() override { return 4; }

    Messages::WindowServer::GetScreenBitmapResponse get_screen_bitmap(Optional<Gfx::IntRect> const&, Optional<u32> const&) override { return Gfx::ShareableBitmap {}; }
    Messages::WindowServer::GetScreenBitmapAroundCursorResponse get_screen_bitmap_around_cursor(Gfx::IntSize) override { return Gfx::ShareableBitmap {}; }
    Messages::WindowServer::GetScreenBitmapAroundLocationResponse get_screen_bitmap_around_location(Gfx::IntSize, Gfx::IntPoint) override { return Gfx::ShareableBitmap {}; }
    Messages::WindowServer::GetColorUnderCursorResponse get_color_under_cursor() override { return OptionalNone(); }

    void set_double_click_speed(i32) override { }
    Messages::WindowServer::GetDoubleClickSpeedResponse get_double_click_speed() override { return 250; }
    void set_mouse_buttons_switched(bool) override { }
    Messages::WindowServer::AreMouseButtonsSwitchedResponse are_mouse_buttons_switched() override { return false; }
    void set_natural_scroll(bool) override { }
    Messages::WindowServer::IsNaturalScrollResponse is_natural_scroll() override { return false; }

    Messages::WindowServer::GetDesktopDisplayScaleResponse get_desktop_display_scale(u32) override { return 1; }
    void set_flash_flush(bool) override { }

    Messages::WindowServer::StartDragResponse start_drag(ByteString const&, HashMap<String, ByteBuffer> const&, Gfx::ShareableBitmap const&) override { return true; }
    void set_accepts_drag(bool) override { }

    void set_window_parent_from_client(i32, i32, i32) override { }
    Messages::WindowServer::GetWindowRectFromClientResponse get_window_rect_from_client(i32, i32) override { return Gfx::IntRect {}; }
    void add_window_stealing_for_client(i32, i32) override { }
    void remove_window_stealing_for_client(i32, i32) override { }
    void remove_window_stealing(i32) override { }

    HashMap<int, WaylandWindowState> m_windows;
    HashMap<int, Menu> m_menus;
    int m_open_menu_id { -1 };
    int m_open_menu_window_id { -1 };

    // Popup menu surfaces
    wl_surface* m_popup_surface { nullptr };
    struct xdg_surface* m_popup_xdg_surface { nullptr };
    xdg_popup* m_popup_xdg_popup { nullptr };
    wl_buffer* m_popup_buffer { nullptr };
    struct PopupItemRect {
        int identifier;
        Gfx::IntRect rect;
    };
    Vector<PopupItemRect> m_popup_item_rects;
};

// =====================================================================
// Wayland listener callbacks
// =====================================================================

// --- Keyboard ---

static void keyboard_keymap(void*, wl_keyboard*, uint32_t format, int fd, uint32_t size)
{
    auto& wl = *s_wayland;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    auto* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (wl.kb_keymap)
        xkb_keymap_unref(wl.kb_keymap);
    if (wl.kb_state)
        xkb_state_unref(wl.kb_state);

    wl.kb_keymap = xkb_keymap_new_from_string(wl.xkb_ctx, map_str,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (wl.kb_keymap)
        wl.kb_state = xkb_state_new(wl.kb_keymap);
}

static void keyboard_enter(void*, wl_keyboard*, uint32_t serial, wl_surface* surface, wl_array*)
{
    s_wayland->keyboard_focus_surface = surface;
    s_wayland->keyboard_serial = serial;

    // Notify the window it's been activated
    auto it = s_surface_to_window.find(surface);
    if (it != s_surface_to_window.end() && it->value) {
        auto* win = it->value;
        if (s_connections) {
            auto conn_it = s_connections->find(win->client_id);
            if (conn_it != s_connections->end())
                conn_it->value->async_window_activated(win->window_id);
        }
    }
}

static void keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface* surface)
{
    if (s_wayland->keyboard_focus_surface == surface)
        s_wayland->keyboard_focus_surface = nullptr;

    auto it = s_surface_to_window.find(surface);
    if (it != s_surface_to_window.end() && it->value) {
        auto* win = it->value;
        if (s_connections) {
            auto conn_it = s_connections->find(win->client_id);
            if (conn_it != s_connections->end())
                conn_it->value->async_window_deactivated(win->window_id);
        }
    }
}

static void keyboard_key(void*, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state)
{
    auto& wl = *s_wayland;
    wl.keyboard_serial = serial;

    if (!wl.kb_state || !wl.keyboard_focus_surface)
        return;

    auto it = s_surface_to_window.find(wl.keyboard_focus_surface);
    if (it == s_surface_to_window.end() || !it->value)
        return;

    auto* win = it->value;
    auto conn_it = s_connections->find(win->client_id);
    if (conn_it == s_connections->end())
        return;

    // XKB uses evdev keycodes which are offset by 8 from Linux keycodes
    uint32_t xkb_keycode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(wl.kb_state, xkb_keycode);
    uint32_t utf32 = xkb_state_key_get_utf32(wl.kb_state, xkb_keycode);

    KeyCode serenity_key = xkb_keysym_to_serenity(sym);

    u8 modifiers = 0;
    if (xkb_state_mod_name_is_active(wl.kb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
        modifiers |= Mod_Shift;
    if (xkb_state_mod_name_is_active(wl.kb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
        modifiers |= Mod_Ctrl;
    if (xkb_state_mod_name_is_active(wl.kb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
        modifiers |= Mod_Alt;
    if (xkb_state_mod_name_is_active(wl.kb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
        modifiers |= Mod_Super;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        conn_it->value->async_key_down(win->window_id, utf32, (u32)serenity_key, 0, modifiers, key);
    else
        conn_it->value->async_key_up(win->window_id, utf32, (u32)serenity_key, 0, modifiers, key);
}

static void keyboard_modifiers(void*, wl_keyboard*, uint32_t,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    auto& wl = *s_wayland;
    if (wl.kb_state)
        xkb_state_update_mask(wl.kb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) { }

static struct wl_keyboard_listener const s_keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

// --- Pointer ---

static void pointer_enter(void*, wl_pointer*, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    s_wayland->pointer_focus_surface = surface;
    s_wayland->pointer_serial = serial;
    s_wayland->pointer_position = { wl_fixed_to_int(x), wl_fixed_to_int(y) };

    auto it = s_surface_to_window.find(surface);
    if (it != s_surface_to_window.end() && it->value) {
        auto* win = it->value;
        if (s_connections) {
            auto conn_it = s_connections->find(win->client_id);
            if (conn_it != s_connections->end())
                conn_it->value->async_window_entered(win->window_id);
        }
    }
}

static void pointer_leave(void*, wl_pointer*, uint32_t, wl_surface* surface)
{
    if (s_wayland->pointer_focus_surface == surface)
        s_wayland->pointer_focus_surface = nullptr;

    auto it = s_surface_to_window.find(surface);
    if (it != s_surface_to_window.end() && it->value) {
        auto* win = it->value;
        if (s_connections) {
            auto conn_it = s_connections->find(win->client_id);
            if (conn_it != s_connections->end())
                conn_it->value->async_window_left(win->window_id);
        }
    }
}

static Gfx::IntPoint adjust_pointer_for_menubar(WaylandWindowState* win, Gfx::IntPoint pos)
{
    return { pos.x(), pos.y() - win->menubar_height() };
}

static bool pointer_in_menubar(WaylandWindowState* win, Gfx::IntPoint pos)
{
    return win->has_menubar() && pos.y() < win->menubar_height();
}

static void pointer_motion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
    auto& wl = *s_wayland;
    wl.pointer_position = { wl_fixed_to_int(x), wl_fixed_to_int(y) };

    if (!wl.pointer_focus_surface)
        return;

    auto it = s_surface_to_window.find(wl.pointer_focus_surface);
    if (it == s_surface_to_window.end() || !it->value)
        return;

    auto* win = it->value;
    if (!s_connections)
        return;
    auto conn_it = s_connections->find(win->client_id);
    if (conn_it == s_connections->end())
        return;

    if (pointer_in_menubar(win, wl.pointer_position))
        return; // In menubar area, don't forward to client

    auto adjusted = adjust_pointer_for_menubar(win, wl.pointer_position);
    conn_it->value->async_mouse_move(win->window_id, adjusted,
        0, wl.pointer_button_state, 0, 0, 0, 0, 0);
}

static void pointer_button(void*, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state)
{
    auto& wl = *s_wayland;
    wl.pointer_serial = serial;

    // Map Linux button codes to Serenity button mask
    // BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112
    unsigned serenity_button = 0;
    if (button == 0x110)
        serenity_button = 1; // Primary
    else if (button == 0x111)
        serenity_button = 2; // Secondary
    else if (button == 0x112)
        serenity_button = 4; // Middle

    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        wl.pointer_button_state |= serenity_button;
    else
        wl.pointer_button_state &= ~serenity_button;

    if (!wl.pointer_focus_surface)
        return;

    // Check if click is on a popup surface
    if (state == WL_POINTER_BUTTON_STATE_PRESSED && serenity_button == 1) {
        for (auto& [_, conn] : *s_connections) {
            if (conn->is_popup_surface(wl.pointer_focus_surface)) {
                conn->handle_popup_click(wl.pointer_position);
                return;
            }
        }
    }

    auto it = s_surface_to_window.find(wl.pointer_focus_surface);
    if (it == s_surface_to_window.end() || !it->value)
        return;

    auto* win = it->value;
    if (!s_connections)
        return;
    auto conn_it = s_connections->find(win->client_id);
    if (conn_it == s_connections->end())
        return;

    if (pointer_in_menubar(win, wl.pointer_position)) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED && serenity_button == 1)
            conn_it->value->handle_menubar_click(win, wl.pointer_position);
        return;
    }

    auto adjusted = adjust_pointer_for_menubar(win, wl.pointer_position);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        conn_it->value->dismiss_open_menu();
        conn_it->value->async_mouse_down(win->window_id, adjusted,
            serenity_button, wl.pointer_button_state, 0, 0, 0, 0, 0);
    } else {
        conn_it->value->async_mouse_up(win->window_id, adjusted,
            serenity_button, wl.pointer_button_state, 0, 0, 0, 0, 0);
    }
}

static void pointer_axis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
{
    auto& wl = *s_wayland;
    if (!wl.pointer_focus_surface)
        return;

    auto it = s_surface_to_window.find(wl.pointer_focus_surface);
    if (it == s_surface_to_window.end() || !it->value)
        return;

    auto* win = it->value;
    if (!s_connections)
        return;
    auto conn_it = s_connections->find(win->client_id);
    if (conn_it == s_connections->end())
        return;

    int delta = wl_fixed_to_int(value);
    int wheel_delta_x = 0, wheel_delta_y = 0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        wheel_delta_y = delta > 0 ? 1 : -1;
    else
        wheel_delta_x = delta > 0 ? 1 : -1;

    if (pointer_in_menubar(win, wl.pointer_position))
        return;
    auto adjusted = adjust_pointer_for_menubar(win, wl.pointer_position);
    conn_it->value->async_mouse_wheel(win->window_id, adjusted,
        0, wl.pointer_button_state, 0, wheel_delta_x, wheel_delta_y, 0, 0);
}

static void pointer_frame(void*, wl_pointer*) { }
static void pointer_axis_source(void*, wl_pointer*, uint32_t) { }
static void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) { }
static void pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) { }
static void pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) { }
static void pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) { }

static struct wl_pointer_listener const s_pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
};

// --- Seat ---

static void seat_capabilities(void*, wl_seat* seat, uint32_t caps)
{
    auto& wl = *s_wayland;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl.keyboard) {
        wl.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wl.keyboard, &s_keyboard_listener, nullptr);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl.pointer) {
        wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(wl.pointer, &s_pointer_listener, nullptr);
    }
}

static void seat_name(void*, wl_seat*, char const*) { }

static struct wl_seat_listener const s_seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// --- XDG WM Base ---

static void xdg_wm_base_ping(void*, xdg_wm_base* base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}

static struct xdg_wm_base_listener const s_xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

// --- Registry ---

static void registry_global(void*, wl_registry* registry, uint32_t name,
    char const* interface, uint32_t version)
{
    auto& wl = *s_wayland;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl.compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, min(version, 4u)));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl.shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        wl.wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, min(version, 2u)));
        xdg_wm_base_add_listener(wl.wm_base, &s_xdg_wm_base_listener, nullptr);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl.seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, min(version, 5u)));
        wl_seat_add_listener(wl.seat, &s_seat_listener, nullptr);
    }
}

static void registry_global_remove(void*, wl_registry*, uint32_t) { }

static struct wl_registry_listener const s_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// =====================================================================
// Main entry point
// =====================================================================

ErrorOr<int> serenity_main(Main::Arguments)
{
    // Set up resource root for fonts and themes
    auto* source_dir = getenv("SERENITY_SOURCE_DIR");
    ByteString resource_root;
    if (source_dir) {
        resource_root = ByteString::formatted("{}/Base/res", source_dir);
    } else {
        auto exe_path = TRY(Core::System::readlink("/proc/self/exe"sv));
        auto exe_dir = LexicalPath(exe_path).parent().string();
        resource_root = ByteString::formatted("{}/../Root/res", exe_dir);
    }
    Core::ResourceImplementation::install(make<Core::ResourceImplementationFile>(
        MUST(String::from_byte_string(resource_root))));

    // Set up fonts (use TTF fonts available in Lagom)
    Gfx::FontDatabase::set_default_font_query("Liberation Sans 10 400 0"sv);
    Gfx::FontDatabase::set_fixed_width_font_query("Liberation Mono 10 400 0"sv);

    // Load theme
    auto theme_path = ByteString::formatted("{}/themes/Default.ini", resource_root);
    auto theme = TRY(Gfx::load_system_theme(theme_path, {}));
    Gfx::set_system_theme(theme);
    s_theme_buffer = move(theme);

    // Initialize Wayland
    WaylandGlobals wayland;
    s_wayland = &wayland;

    wayland.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!wayland.xkb_ctx) {
        warnln("Failed to create XKB context");
        return 1;
    }

    wayland.display = wl_display_connect(nullptr);
    if (!wayland.display) {
        warnln("Failed to connect to Wayland display");
        return 1;
    }

    wayland.registry = wl_display_get_registry(wayland.display);
    wl_registry_add_listener(wayland.registry, &s_registry_listener, nullptr);
    wl_display_roundtrip(wayland.display);

    if (!wayland.compositor || !wayland.shm || !wayland.wm_base) {
        warnln("Wayland compositor missing required globals (compositor, shm, xdg_wm_base)");
        return 1;
    }

    // Set up Core::EventLoop
    Core::EventLoop event_loop;

    // Integrate Wayland display fd with the event loop
    int wl_fd = wl_display_get_fd(wayland.display);
    auto wl_notifier = Core::Notifier::construct(wl_fd, Core::Notifier::Type::Read);
    wl_notifier->on_activation = [&] {
        // Must prepare_read/cancel before dispatch to avoid blocking
        while (wl_display_prepare_read(wayland.display) != 0)
            wl_display_dispatch_pending(wayland.display);
        wl_display_read_events(wayland.display);
        wl_display_dispatch_pending(wayland.display);
        wl_display_flush(wayland.display);
    };

    // Set up IPC server socket
    auto socket_path = TRY(Core::SessionManagement::parse_path_with_sid("/tmp/portal/window"sv));

    // Ensure the directory exists
    auto dir_path = LexicalPath(socket_path).parent().string();
    if (mkdir(dir_path.characters(), 0755) < 0 && errno != EEXIST) {
        warnln("Failed to create directory {}: {}", dir_path, strerror(errno));
        return 1;
    }

    // Remove stale socket if present
    unlink(socket_path.characters());

    auto server = TRY(Core::LocalServer::try_create());
    if (!server->listen(socket_path)) {
        warnln("Failed to listen on {}", socket_path);
        return 1;
    }

    server->on_accept = [&](NonnullOwnPtr<Core::LocalSocket> client_socket) {
        int cid = ++s_next_client_id;
        auto conn = IPC::new_client_connection<ClientConnection>(move(client_socket), cid);
        dbgln("WindowServerWayland: New client connection (id={})", cid);
    };

    dbgln("WindowServerWayland: Listening on {}", socket_path);
    dbgln("WindowServerWayland: Ready for connections");

    // Flush any pending Wayland events before entering the loop
    wl_display_flush(wayland.display);

    // Periodically flush Wayland display
    auto flush_timer = Core::Timer::create_repeating(16, [&] {
        wl_display_flush(wayland.display);
    });
    flush_timer->start();

    event_loop.exec();

    // Cleanup
    if (wayland.keyboard)
        wl_keyboard_destroy(wayland.keyboard);
    if (wayland.pointer)
        wl_pointer_destroy(wayland.pointer);
    if (wayland.seat)
        wl_seat_destroy(wayland.seat);
    if (wayland.wm_base)
        xdg_wm_base_destroy(wayland.wm_base);
    if (wayland.shm)
        wl_shm_destroy(wayland.shm);
    if (wayland.compositor)
        wl_compositor_destroy(wayland.compositor);
    if (wayland.registry)
        wl_registry_destroy(wayland.registry);
    wl_display_disconnect(wayland.display);

    if (wayland.kb_state)
        xkb_state_unref(wayland.kb_state);
    if (wayland.kb_keymap)
        xkb_keymap_unref(wayland.kb_keymap);
    xkb_context_unref(wayland.xkb_ctx);

    return 0;
}

/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Standalone ClipboardServer for Lagom - uses listen() instead of SystemServer takeover.

#include <Clipboard/ConnectionFromClient.h>
#include <Clipboard/Storage.h>
#include <LibCore/EventLoop.h>
#include <LibCore/LocalServer.h>
#include <LibCore/SessionManagement.h>
#include <LibCore/System.h>
#include <LibMain/Main.h>

#include <sys/stat.h>
#include <unistd.h>

ErrorOr<int> serenity_main(Main::Arguments)
{
    Core::EventLoop event_loop;

    auto socket_path = TRY(Core::SessionManagement::parse_path_with_sid("/tmp/session/%sid/portal/clipboard"sv));

    // Ensure directory exists
    auto dir = socket_path.substring(0, socket_path.find_last('/').value());
    mkdir(dir.characters(), 0755);
    unlink(socket_path.characters());

    auto server = TRY(Core::LocalServer::try_create());
    if (!server->listen(socket_path)) {
        warnln("ClipboardServer: Failed to listen on {}", socket_path);
        return 1;
    }

    static int next_client_id = 0;
    server->on_accept = [&](auto client_socket) {
        auto client_id = ++next_client_id;
        auto client = IPC::new_client_connection<Clipboard::ConnectionFromClient>(move(client_socket), client_id);
        (void)client;
    };

    Clipboard::Storage::the().on_content_change = [&] {
        Clipboard::ConnectionFromClient::for_each_client([&](auto& client) {
            client.notify_about_clipboard_change();
        });
    };

    dbgln("ClipboardServer: Listening on {}", socket_path);
    return event_loop.exec();
}

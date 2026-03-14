/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Standalone ConfigServer for Lagom - uses listen() instead of SystemServer takeover.

#include "ConnectionFromClient.h"
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

    auto socket_path = TRY(Core::SessionManagement::parse_path_with_sid("/tmp/session/%sid/portal/config"sv));

    // Ensure directory exists
    auto dir = socket_path.substring(0, socket_path.find_last('/').value());
    // Create parent directories recursively
    for (size_t i = 1; i < dir.length(); i++) {
        if (dir[i] == '/') {
            auto partial = dir.substring(0, i);
            mkdir(partial.characters(), 0755);
        }
    }
    mkdir(dir.characters(), 0755);
    unlink(socket_path.characters());

    auto server = TRY(Core::LocalServer::try_create());
    if (!server->listen(socket_path)) {
        warnln("ConfigServer: Failed to listen on {}", socket_path);
        return 1;
    }

    static int next_client_id = 0;
    server->on_accept = [&](auto client_socket) {
        auto client_id = ++next_client_id;
        auto client = IPC::new_client_connection<ConfigServer::ConnectionFromClient>(move(client_socket), client_id);
        (void)client;
    };

    dbgln("ConfigServer: Listening on {}", socket_path);
    return event_loop.exec();
}

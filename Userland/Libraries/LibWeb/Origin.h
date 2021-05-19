/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/URL.h>

namespace Web {

class Origin {
public:
    Origin() { }
    Origin(const String& protocol, const String& host, u16 port)
        : m_protocol(protocol)
        , m_host(host)
        , m_port(port)
    {
    }

    // https://url.spec.whatwg.org/#concept-url-origin
    static Origin create_from_url(const URL& url)
    {
        // FIXME: Handle blob and file

        if (url.protocol().is_one_of("ftp", "http", "https", "ws", "wss"))
            return { url.protocol(), url.host(), url.port() };

        return {};
    }

    bool is_null() const { return m_protocol.is_null() && m_host.is_null() && !m_port; }

    const String& protocol() const { return m_protocol; }
    const String& host() const { return m_host; }
    u16 port() const { return m_port; }

    // https://html.spec.whatwg.org/multipage/origin.html#same-origin
    bool is_same(const Origin& other) const
    {
        return protocol() == other.protocol()
            && host() == other.host()
            && port() == other.port();
    }

    // https://html.spec.whatwg.org/multipage/origin.html#ascii-serialisation-of-an-origin
    String serialize() const
    {
        if (is_null())
            return "null";

        StringBuilder builder;
        builder.append(m_protocol);
        builder.append("://");
        builder.append(m_host);

        // FIXME: Being 0 is not the same as null.
        if (m_port) {
            builder.append(':');
            builder.appendff("{}", m_port);
        }

        return builder.to_string();
    }

private:
    String m_protocol;
    String m_host;
    u16 m_port { 0 }; // FIXME: This should be nullable, and null by default
    // FIXME: A nullable domain
};

}

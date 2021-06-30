/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibCore/DateTime.h>
#include <LibCore/Timer.h>
#include <LibCrypto/ASN1/DER.h>
#include <LibCrypto/PK/Code/EMSA_PSS.h>
#include <LibTLS/TLSv12.h>

namespace TLS {

Optional<ByteBuffer> TLSv12::read()
{
    if (m_context.application_buffer.size()) {
        auto buf = m_context.application_buffer.slice(0, m_context.application_buffer.size());
        m_context.application_buffer.clear();
        return buf;
    }
    return {};
}

size_t TLSv12::read(Bytes bytes)
{
    if (m_context.application_buffer.size()) {
        auto length = min(m_context.application_buffer.size(), bytes.size());
        memcpy(bytes.data(), m_context.application_buffer.data(), length);
        m_context.application_buffer = m_context.application_buffer.slice(length, m_context.application_buffer.size() - length);
        return length;
    }
    return {};
}

String TLSv12::read_line(size_t max_size)
{
    if (!can_read_line())
        return {};

    auto* start = m_context.application_buffer.data();
    auto* newline = (u8*)memchr(m_context.application_buffer.data(), '\n', m_context.application_buffer.size());
    VERIFY(newline);

    size_t offset = newline - start;

    if (offset > max_size)
        return {};

    auto buffer = ByteBuffer::copy(start, offset);
    m_context.application_buffer = m_context.application_buffer.slice(offset + 1, m_context.application_buffer.size() - offset - 1);

    return String::copy(buffer, Chomp);
}

size_t TLSv12::write(ReadonlyBytes buffer)
{
    if (m_context.connection_status != ConnectionStatus::Established) {
        dbgln_if(TLS_DEBUG, "write request while not connected");
        return 0;
    }

    PacketBuilder builder { MessageType::ApplicationData, m_context.options.version, buffer.size() };
    builder.append(buffer);
    auto packet = builder.build();

    update_packet(packet);
    write_packet(packet);

    return buffer.size();
}

bool TLSv12::connect(const String& hostname, int port)
{
    set_sni(hostname);
    dbgln("Setting up connection to {}:{}", hostname, port);
    if (!setup_connection())
        return false;
    if (m_transport->is_connected())
        return true;
    return m_transport->connect(hostname, port);
}

bool TLSv12::connect(const Core::SocketAddress& address)
{
    if (!setup_connection())
        return false;
    if (m_transport->is_connected())
        return true;
    return m_transport->connect(address);
}

bool TLSv12::connect(const Core::SocketAddress& address, int port)
{
    if (!setup_connection())
        return false;
    if (m_transport->is_connected())
        return true;
    return m_transport->connect(address, port);
}

bool TLSv12::setup_connection()
{
    if (m_context.critical_error)
        return false;

    m_transport->on_connected = [this] {
        m_transport->on_ready_to_read = [this] {
            read_from_socket();
        };

        auto packet = build_hello();
        write_packet(packet);

        deferred_invoke([&](auto&) {
            m_handshake_timeout_timer = Core::Timer::create_single_shot(
                m_max_wait_time_for_handshake_in_seconds * 1000, [&] {
                    auto timeout_diff = Core::DateTime::now().timestamp() - m_context.handshake_initiation_timestamp;
                    // If the timeout duration was actually within the max wait time (with a margin of error),
                    // we're not operating slow, so the server timed out.
                    // otherwise, it's our fault that the negotiation is taking too long, so extend the timer :P
                    if (timeout_diff < m_max_wait_time_for_handshake_in_seconds + 1) {
                        // The server did not respond fast enough,
                        // time the connection out.
                        alert(AlertLevel::Critical, AlertDescription::UserCanceled);
                        m_context.connection_finished = true;
                        m_context.tls_buffer.clear();
                        m_context.error_code = Error::TimedOut;
                        m_context.critical_error = (u8)Error::TimedOut;
                        check_connection_state(false); // Notify the client.
                    } else {
                        // Extend the timer, we are too slow.
                        m_handshake_timeout_timer->restart(m_max_wait_time_for_handshake_in_seconds * 1000);
                    }
                },
                this);
            write_into_socket();
            m_handshake_timeout_timer->start();
            m_context.handshake_initiation_timestamp = Core::DateTime::now().timestamp();
        });
        m_has_scheduled_write_flush = true;

        if (on_connected)
            on_connected();
    };

    return true;
}

void TLSv12::read_from_socket()
{
    auto did_schedule_read = false;
    auto notify_client_for_app_data = [&] {
        if (m_context.application_buffer.size() > 0) {
            if (!did_schedule_read) {
                deferred_invoke([&](auto&) { read_from_socket(); });
                did_schedule_read = true;
            }
            if (on_ready_to_read)
                on_ready_to_read();

            for (auto& object : m_vended_notifiers) {
                auto& notifier = static_cast<TLSNotifier&>(*object);
                if (notifier.on_ready_to_read && notifier.is_enabled(Core::AbstractNotifier::Event::Read))
                    notifier.on_ready_to_read();
            }
        }
    };

    // If there's anything before we consume stuff, let the client know
    // since we won't be consuming things if the connection is terminated.
    notify_client_for_app_data();

    if (!check_connection_state(true))
        return;

    char buf[4096];
    auto nread = m_transport->read({ buf, 4096 });
    consume({ buf, nread });
    // If anything new shows up, tell the client about the event.
    notify_client_for_app_data();
}

void TLSv12::write_into_socket()
{
    dbgln_if(TLS_DEBUG, "Flushing cached records: {} established? {}", m_context.tls_buffer.size(), is_established());

    m_has_scheduled_write_flush = false;
    if (!check_connection_state(false))
        return;
    flush();

    if (!is_established())
        return;

    if (!m_context.application_buffer.size()) { // hey client, you still have stuff to read...
        if (on_tls_ready_to_write)
            on_tls_ready_to_write(*this);

        for (auto& object : m_vended_notifiers) {
            auto& notifier = static_cast<TLSNotifier&>(*object);
            if (notifier.on_ready_to_write && notifier.is_enabled(Core::AbstractNotifier::Event::Write))
                notifier.on_ready_to_write();
        }
    }
}

bool TLSv12::check_connection_state(bool read)
{
    if (!m_transport->is_connected()) {
        // an abrupt closure (the server is a jerk)
        dbgln_if(TLS_DEBUG, "Socket not open, assuming abrupt closure");
        m_context.connection_finished = true;
    } else if (m_transport->unreliable_eof()) {
        // Treat this as connection finished only if there's nothing to send (i.e. the server is waiting for us to say something)
        if (m_context.tls_buffer.is_empty())
            m_context.connection_finished = true;
    }
    if (m_context.critical_error) {
        dbgln_if(TLS_DEBUG, "CRITICAL ERROR {} :(", m_context.critical_error);

        if (on_tls_error)
            on_tls_error((AlertDescription)m_context.critical_error);
        return false;
    }
    if (((read && m_context.application_buffer.size() == 0) || !read) && m_context.connection_finished) {
        if (m_context.application_buffer.size() == 0 && m_context.connection_status != ConnectionStatus::Disconnected) {
            if (on_tls_finished)
                on_tls_finished();
        }
        if (m_context.tls_buffer.size()) {
            dbgln_if(TLS_DEBUG, "connection closed without finishing data transfer, {} bytes still in buffer and {} bytes in application buffer",
                m_context.tls_buffer.size(),
                m_context.application_buffer.size());
        } else {
            m_context.connection_finished = false;
            dbgln_if(TLS_DEBUG, "FINISHED");
        }
        if (!m_context.application_buffer.size()) {
            m_context.connection_status = ConnectionStatus::Disconnected;
            m_transport->shutdown();
            return false;
        }
    }
    return true;
}

bool TLSv12::flush()
{
    auto out_buffer = write_buffer().data();
    size_t out_buffer_index { 0 };
    size_t out_buffer_length = write_buffer().size();

    if (out_buffer_length == 0)
        return true;

    if constexpr (TLS_DEBUG) {
        dbgln("SENDING...");
        print_buffer(out_buffer, out_buffer_length);
    }
    auto bytes_written = m_transport->write({ &out_buffer[out_buffer_index], out_buffer_length });
    if (bytes_written == out_buffer_length) {
        write_buffer().clear();
        return true;
    }
    write_buffer() = write_buffer().slice(bytes_written, write_buffer().size() - bytes_written);
    if (m_context.send_retries++ == 10) {
        // drop the records, we can't send
        dbgln_if(TLS_DEBUG, "Dropping {} bytes worth of TLS records as max retries has been reached", write_buffer().size());
        write_buffer().clear();
        m_context.send_retries = 0;
    }
    return false;
}

}

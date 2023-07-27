/*
 * Copyright (c) 2020-2022, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/MaybeOwned.h>
#include <AK/Optional.h>
#include <AK/Queue.h>
#include <LibCore/NetworkJob.h>
#include <LibCore/Socket.h>
#include <LibHTTP/HttpRequest.h>
#include <LibHTTP/HttpResponse.h>

namespace HTTP {

class Job : public Core::NetworkJob {
    C_OBJECT(Job);

public:
    explicit Job(HttpRequest&&, Stream&);
    virtual ~Job() override = default;

    virtual void start(Core::BufferedSocketBase&) override;
    virtual void shutdown(ShutdownMode) override;

    Core::Socket const* socket() const { return m_socket; }
    URL url() const { return m_request.url(); }

    HttpResponse* response() { return static_cast<HttpResponse*>(Core::NetworkJob::response()); }
    HttpResponse const* response() const { return static_cast<HttpResponse const*>(Core::NetworkJob::response()); }

protected:
    void finish_up();
    void on_socket_connected();
    void flush_received_buffers();
    void register_on_ready_to_read(Function<void()>);
    ErrorOr<DeprecatedString> read_line(size_t);
    ErrorOr<ByteBuffer> receive(size_t);
    void timer_event(Core::TimerEvent&) override;

    enum class State {
        InStatus,
        InHeaders,
        InBody,
        Trailers,
        Finished,
    };

    HttpRequest m_request;
    State m_state { State::InStatus };
    Core::BufferedSocketBase* m_socket { nullptr };
    bool m_legacy_connection { false };
    int m_code { -1 };
    HashMap<DeprecatedString, DeprecatedString, CaseInsensitiveStringTraits> m_headers;
    Vector<DeprecatedString> m_set_cookie_headers;

    struct ReceivedBuffer {
        ReceivedBuffer(ByteBuffer d)
            : data(move(d))
            , pending_flush(data.bytes())
        {
        }

        // The entire received buffer.
        ByteBuffer data;

        // The bytes we have yet to flush. (This is a slice of `data`)
        ReadonlyBytes pending_flush;
    };

    class BufferingStream final : public Stream {
    public:
        ErrorOr<Bytes> read_some(Bytes bytes) override
        {
            size_t total_read = 0;
            TRY(try_flush_into([&](ReadonlyBytes read_bytes) -> ErrorOr<size_t> {
                auto read_count = min(read_bytes.size(), bytes.size() - total_read);
                read_bytes.slice(0, read_count).copy_to(bytes.slice(total_read, read_count));
                total_read += read_count;
                return read_count;
            }));
            return bytes.slice(0, total_read);
        }

        ErrorOr<size_t> write_some(ReadonlyBytes) override
        {
            // Use .write_buffer(ByteBuffer&&) instead.
            return AK::Error::from_errno(ENOTSUP);
        }

        ErrorOr<void> write_buffer(ByteBuffer&& buffer)
        {
            if (buffer.is_empty())
                return {};

            m_received_buffers.enqueue(make<ReceivedBuffer>(move(buffer)));
            return {};
        }

        template<CallableAs<ErrorOr<size_t>, ReadonlyBytes> F>
        ErrorOr<size_t> try_flush_into(F f)
        {
            if (m_received_buffers.is_empty())
                return 0;

            size_t total_flushed = 0;
            while (!m_received_buffers.is_empty()) {
                auto& buffer = m_received_buffers.head();
                auto result = f(buffer->pending_flush);
                if (result.is_error()) {
                    if (!result.error().is_errno())
                        return result.release_error();
                    if (result.error().code() == EINTR)
                        continue;
                    if (result.error().code() == EAGAIN)
                        break;
                    return result.release_error();
                }

                auto read_count = result.release_value();
                if (read_count == 0)
                    return total_flushed;

                total_flushed += read_count;

                buffer->pending_flush = buffer->pending_flush.slice(read_count);
                if (buffer->pending_flush.is_empty())
                    (void)m_received_buffers.dequeue();
            };

            return total_flushed;
        }

        bool is_eof() const override { return m_received_buffers.is_empty(); }

        bool is_open() const override { return true; }

        void close() override { m_received_buffers.clear(); }

        size_t buffer_count() const { return m_received_buffers.size(); }

    private:
        Queue<NonnullOwnPtr<ReceivedBuffer>> m_received_buffers;
    };

    struct DecodingStream {
        template<typename MakeDecompressor>
        DecodingStream(MakeDecompressor f)
            : stream(f(MaybeOwned<Stream>(input_stream)))
        {
        }

        MaybeOwned<Stream> stream;
        BufferingStream input_stream;
    };

    size_t m_buffered_size { 0 };
    size_t m_received_size { 0 };
    Optional<u64> m_content_length;
    Optional<ssize_t> m_current_chunk_remaining_size;
    Optional<size_t> m_current_chunk_total_size;
    Optional<DecodingStream> m_decoding_stream;
    BufferingStream m_buffering_stream;
    bool m_should_read_chunk_ending_line { false };
    bool m_has_scheduled_finish { false };
};

}

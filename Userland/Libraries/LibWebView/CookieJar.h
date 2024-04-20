/*
 * Copyright (c) 2021-2024, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Traits.h>
#include <LibCore/DateTime.h>
#include <LibCore/Timer.h>
#include <LibSQL/Type.h>
#include <LibURL/Forward.h>
#include <LibWeb/Cookie/Cookie.h>
#include <LibWeb/Forward.h>
#include <LibWebView/Forward.h>

namespace WebView {

struct CookieStorageKey {
    bool operator==(CookieStorageKey const&) const = default;

    String name;
    String domain;
    String path;
};

struct CookieCacheStorageKey {
    bool operator==(CookieCacheStorageKey const& other) const
    {
        // Ignore creation_time when comparing keys.
        return name == other.name && domain == other.domain && path == other.path;
    }

    String name;
    String domain;
    String path;
    MonotonicTime creation_time;
};

class CookieJar {
    struct Statements {
        SQL::StatementID create_table { 0 };
        SQL::StatementID insert_cookie { 0 };
        SQL::StatementID update_cookie { 0 };
        SQL::StatementID update_cookie_last_access_time { 0 };
        SQL::StatementID expire_cookie { 0 };
        SQL::StatementID select_cookie { 0 };
        SQL::StatementID select_all_cookies { 0 };
        SQL::StatementID select_all_keys { 0 };
    };

    using TransientStorage = HashMap<CookieStorageKey, Web::Cookie::Cookie>;

    class PersistedStorage : public RefCounted<PersistedStorage> {
        static constexpr Duration CookieExpiryInterval = Duration::from_seconds(1);
        static constexpr Duration CachePurgeInterval = Duration::from_milliseconds(500);
        static constexpr Duration WriteSyncInterval = Duration::from_seconds(5);

    public:
        PersistedStorage(Database& database, Statements statements)
            : database(database)
            , statements(statements)
        {
            write_sync_timer = Core::Timer::create_repeating(WriteSyncInterval.to_milliseconds(), [this] { dump_cookies(); });
            purge_timer = Core::Timer::create_repeating(CachePurgeInterval.to_milliseconds(), [this] { purge_expired_cookies(); });
        }

        ~PersistedStorage()
        {
            write_sync_timer->stop();
            purge_timer->stop();
            dump_cookies();
        }

        void purge_expired_cookies()
        {
            auto expiry_time = MonotonicTime::now() - CookieExpiryInterval;

            Vector<CookieCacheStorageKey> expired_cookies;
            for (auto& it : storage) {
                if (it.value.is_dirty == CookieInfo::IsDirty::No && it.key.creation_time < expiry_time) {
                    expired_cookies.append(it.key);
                }
            }

            for (auto& key : expired_cookies) {
                storage.remove(key);
            }
        }

        void dump_cookies();

        void set(CookieStorageKey const& key, Web::Cookie::Cookie cookie, bool was_fetched_fresh)
        {
            storage.set({ key.domain, key.name, key.path, MonotonicTime::now() }, { move(cookie), was_fetched_fresh ? CookieInfo::IsDirty::No : CookieInfo::IsDirty::Yes });
            dirty_cookies.set(key);
            purge_timer->start();
            write_sync_timer->start();
        }

        Optional<Web::Cookie::Cookie const&> find(CookieStorageKey const& key) const
        {
            auto it = storage.find(CookieCacheStorageKey { key.domain, key.name, key.path, MonotonicTime::now() });
            if (it == storage.end())
                return {};
            return it->value.cookie;
        }

        struct CookieInfo {
            Web::Cookie::Cookie cookie;
            enum class IsDirty : bool {
                No,
                Yes,
            } is_dirty;
        };

        Database& database;
        Statements statements;
        HashMap<CookieCacheStorageKey, CookieInfo> storage;
        HashTable<CookieStorageKey> dirty_cookies;
        RefPtr<Core::Timer> purge_timer;
        RefPtr<Core::Timer> write_sync_timer;
    };

public:
    static ErrorOr<CookieJar> create(Database&);
    static CookieJar create();

    String get_cookie(const URL::URL& url, Web::Cookie::Source source);
    void set_cookie(const URL::URL& url, Web::Cookie::ParsedCookie const& parsed_cookie, Web::Cookie::Source source);
    void update_cookie(Web::Cookie::Cookie);
    void dump_cookies();
    Vector<Web::Cookie::Cookie> get_all_cookies();
    Vector<Web::Cookie::Cookie> get_all_cookies(URL::URL const& url);
    Optional<Web::Cookie::Cookie> get_named_cookie(URL::URL const& url, StringView name);

private:
    explicit CookieJar(NonnullRefPtr<PersistedStorage>);
    explicit CookieJar(TransientStorage);

    static Optional<String> canonicalize_domain(const URL::URL& url);
    static bool domain_matches(StringView string, StringView domain_string);
    static bool path_matches(StringView request_path, StringView cookie_path);
    static String default_path(const URL::URL& url);

    enum class MatchingCookiesSpecMode {
        RFC6265,
        WebDriver,
    };

    void store_cookie(Web::Cookie::ParsedCookie const& parsed_cookie, const URL::URL& url, String canonicalized_domain, Web::Cookie::Source source);
    Vector<Web::Cookie::Cookie> get_matching_cookies(const URL::URL& url, StringView canonicalized_domain, Web::Cookie::Source source, MatchingCookiesSpecMode mode = MatchingCookiesSpecMode::RFC6265);

    void insert_cookie_into_database(Web::Cookie::Cookie const& cookie);
    void update_cookie_in_database(Web::Cookie::Cookie const& cookie);
    void update_cookie_last_access_time_in_database(Web::Cookie::Cookie const& cookie);

    using OnCookieFound = Function<void(Web::Cookie::Cookie&, Web::Cookie::Cookie)>;
    using OnCookieNotFound = Function<void(Web::Cookie::Cookie)>;
    void select_cookie_from_database(Web::Cookie::Cookie cookie, OnCookieFound on_result, OnCookieNotFound on_complete_without_results);

    using OnSelectAllCookiesResult = Function<void(Web::Cookie::Cookie)>;
    void select_all_cookies_from_database(OnSelectAllCookiesResult on_result);

    void purge_expired_cookies();

    Variant<NonnullRefPtr<PersistedStorage>, TransientStorage> m_storage;
};

}

template<>
struct AK::Traits<WebView::CookieStorageKey> : public AK::DefaultTraits<WebView::CookieStorageKey> {
    static unsigned hash(WebView::CookieStorageKey const& key)
    {
        unsigned hash = 0;
        hash = pair_int_hash(hash, key.name.hash());
        hash = pair_int_hash(hash, key.domain.hash());
        hash = pair_int_hash(hash, key.path.hash());
        return hash;
    }
};

template<>
struct AK::Traits<WebView::CookieCacheStorageKey> : public AK::DefaultTraits<WebView::CookieCacheStorageKey> {
    static unsigned hash(WebView::CookieCacheStorageKey const& key)
    {
        unsigned hash = 0;
        hash = pair_int_hash(hash, key.name.hash());
        hash = pair_int_hash(hash, key.domain.hash());
        hash = pair_int_hash(hash, key.path.hash());
        return hash;
    }
};

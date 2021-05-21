/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/LexicalPath.h>
#include <AK/SourceGenerator.h>
#include <LibGemini/Document.h>
#include <LibGfx/ImageDecoder.h>
#include <LibMarkdown/Document.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/ElementFactory.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/Fetch/FrameLoader.h>
#include <LibWeb/Fetch/ResourceLoader.h>
#include <LibWeb/HTML/HTMLIFrameElement.h>
#include <LibWeb/HTML/Parser/HTMLDocumentParser.h>
#include <LibWeb/Namespace.h>
#include <LibWeb/Page/BrowsingContext.h>
#include <LibWeb/Page/Page.h>

namespace Web::Fetch {

FrameLoader::FrameLoader(BrowsingContext& browsing_context)
    : m_browsing_context(browsing_context)
{
}

FrameLoader::~FrameLoader()
{
}

static bool build_markdown_document(DOM::Document& document, const ByteBuffer& data)
{
    auto markdown_document = Markdown::Document::parse(data);
    if (!markdown_document)
        return false;

    HTML::HTMLDocumentParser parser(document, markdown_document->render_to_html(), "utf-8");
    parser.run(document.url());
    return true;
}

static bool build_text_document(DOM::Document& document, const ByteBuffer& data)
{
    auto html_element = document.create_element("html");
    document.append_child(html_element);

    auto head_element = document.create_element("head");
    html_element->append_child(head_element);
    auto title_element = document.create_element("title");
    head_element->append_child(title_element);

    auto title_text = document.create_text_node(document.url().basename());
    title_element->append_child(title_text);

    auto body_element = document.create_element("body");
    html_element->append_child(body_element);

    auto pre_element = document.create_element("pre");
    body_element->append_child(pre_element);

    pre_element->append_child(document.create_text_node(String::copy(data)));
    return true;
}

static bool build_image_document(DOM::Document& document, const ByteBuffer& data)
{
    auto image_decoder = Gfx::ImageDecoder::create(data.data(), data.size());
    auto bitmap = image_decoder->bitmap();
    if (!bitmap)
        return false;

    auto html_element = document.create_element("html");
    document.append_child(html_element);

    auto head_element = document.create_element("head");
    html_element->append_child(head_element);
    auto title_element = document.create_element("title");
    head_element->append_child(title_element);

    auto basename = LexicalPath(document.url().path()).basename();
    auto title_text = adopt_ref(*new DOM::Text(document, String::formatted("{} [{}x{}]", basename, bitmap->width(), bitmap->height())));
    title_element->append_child(title_text);

    auto body_element = document.create_element("body");
    html_element->append_child(body_element);

    auto image_element = document.create_element("img");
    image_element->set_attribute(HTML::AttributeNames::src, document.url().to_string());
    body_element->append_child(image_element);

    return true;
}

static bool build_gemini_document(DOM::Document& document, const ByteBuffer& data)
{
    StringView gemini_data { data };
    auto gemini_document = Gemini::Document::parse(gemini_data, document.url());
    String html_data = gemini_document->render_to_html();

    dbgln_if(GEMINI_DEBUG, "Gemini data:\n\"\"\"{}\"\"\"", gemini_data);
    dbgln_if(GEMINI_DEBUG, "Converted to HTML:\n\"\"\"{}\"\"\"", html_data);

    HTML::HTMLDocumentParser parser(document, html_data, "utf-8");
    parser.run(document.url());
    return true;
}

bool FrameLoader::parse_document(DOM::Document& document, const ByteBuffer& data)
{
    auto& mime_type = document.content_type();
    if (mime_type == "text/html" || mime_type == "image/svg+xml") {
        auto parser = HTML::HTMLDocumentParser::create_with_uncertain_encoding(document, data);
        parser->run(document.url());
        return true;
    }
    if (mime_type.starts_with("image/"))
        return build_image_document(document, data);
    if (mime_type == "text/plain" || mime_type == "application/json")
        return build_text_document(document, data);
    if (mime_type == "text/markdown")
        return build_markdown_document(document, data);
    if (mime_type == "text/gemini")
        return build_gemini_document(document, data);

    return false;
}

bool FrameLoader::load(const LoadRequest& request, Type type)
{
    if (!request.is_valid()) {
        load_error_page(request.url(), "Invalid request");
        return false;
    }

    if (!m_browsing_context.is_frame_nesting_allowed(request.url())) {
        dbgln("No further recursion is allowed for the frame, abort load!");
        return false;
    }

    auto& url = request.url();

    // FIXME: HACK
    auto resource = ResourceLoader::the().load_resource(Response::Type::Generic, request);
    Vector<URL> urls;
    urls.append(request.url());
    resource->set_url_list(urls);

    set_resource(resource);

    if (type == Type::Navigation || type == Type::Reload) {
        if (auto* page = browsing_context().page())
            page->client().page_did_start_loading(url);
    }

    set_resource(ResourceLoader::the().load_resource(Resource::Type::Generic, request));

    if (type == Type::IFrame)
        return true;

    if (url.protocol() == "http" || url.protocol() == "https") {
        URL favicon_url;
        favicon_url.set_protocol(url.protocol());
        favicon_url.set_host(url.host());
        favicon_url.set_port(url.port());
        favicon_url.set_paths({ "favicon.ico" });

        ResourceLoader::the().load(
            favicon_url,
            [this, favicon_url](auto data, auto&, auto) {
                dbgln("Favicon downloaded, {} bytes from {}", data.size(), favicon_url);
                auto decoder = Gfx::ImageDecoder::create(data.data(), data.size());
                auto bitmap = decoder->bitmap();
                if (!bitmap) {
                    dbgln("Could not decode favicon {}", favicon_url);
                    return;
                }
                dbgln("Decoded favicon, {}", bitmap->size());
                if (auto* page = browsing_context().page())
                    page->client().page_did_change_favicon(*bitmap);
            });
    }

    return true;
}

bool FrameLoader::load(const URL& url, Type type)
{
    dbgln("FrameLoader::load: {}", url);

    if (!url.is_valid()) {
        load_error_page(url, "Invalid URL");
        return false;
    }

    auto request = LoadRequest::create_for_url_on_page(url, browsing_context().page());
    return load(request, type);
}

void FrameLoader::load_html(const StringView& html, const URL& url)
{
    auto document = DOM::Document::create(url);
    HTML::HTMLDocumentParser parser(document, html, "utf-8");
    parser.run(url);
    browsing_context().set_document(&parser.document());
}

// FIXME: Use an actual templating engine (our own one when it's built, preferably
// with a way to check these usages at compile time)

void FrameLoader::load_error_page(const URL& failed_url, const String& error)
{
    auto error_page_url = "file:///res/html/error.html";
    ResourceLoader::the().load(
        error_page_url,
        [this, failed_url, error](auto data, auto&, auto) {
            VERIFY(!data.is_null());
            StringBuilder builder;
            SourceGenerator generator { builder };
            generator.set("failed_url", escape_html_entities(failed_url.to_string()));
            generator.set("error", escape_html_entities(error));
            generator.append(data);
            auto document = HTML::parse_html_document(generator.as_string_view(), failed_url, "utf-8");
            VERIFY(document);
            browsing_context().set_document(document);
        },
        [](auto& error, auto) {
            dbgln("Failed to load error page: {}", error);
            VERIFY_NOT_REACHED();
        });
}

void FrameLoader::resource_did_load()
{
    // FIXME: Don't blindly take value.
    dbgln("url has value? {}", resource()->url().has_value());
    auto url = resource()->url().value();
    dbgln("url: {}", url.to_string());

    // FIXME: Also check HTTP status code before redirecting
    auto location = resource()->header_list().get("Location");
    if (!location.is_null()) {
        if (m_redirects_count > maximum_redirects_allowed) {
            m_redirects_count = 0;
            load_error_page(url, "Too many redirects");
            return;
        }
        m_redirects_count++;
        load(url.complete_url(location), FrameLoader::Type::Navigation);
        return;
    }
    m_redirects_count = 0;

    if (!resource()->has_encoded_data()) {
        load_error_page(url, "No data");
        return;
    }

    auto mime_type = resource()->mime_type();
    String encoding;

    if (mime_type.has_value()) {
        auto charset_optional = mime_type.value().parameters().get("charset");

        if (charset_optional.has_value())
            encoding = charset_optional.value();

        if (!encoding.is_null()) {
            dbgln("This content has MIME type '{}', encoding '{}'", mime_type.value().essence(), encoding);
        } else {
            dbgln("This content has MIME type '{}', encoding unknown", mime_type.value().essence());
        }
    }


    auto document = DOM::Document::create();
    document->set_url(url);
    document->set_encoding("windows-1252");

    if (mime_type.has_value())
        document->set_content_type(mime_type.value().essence());
    else
        document->set_content_type("text/html");

    browsing_context().set_document(document);

    dbgln("size {}", resource()->body().size());
    if (!parse_document(*document, ByteBuffer::copy(resource()->body()))) {
        load_error_page(url, "Failed to parse content.");
        return;
    }

    // FIXME: Support multiple instances of the Set-Cookie response header.
    // FIXME: Set-Cookie shouldn't be done here, it should be done in HTTP-network fetch.
    auto set_cookie = resource()->header_list().get("Set-Cookie");
    if (!set_cookie.is_null())
        document->set_cookie(set_cookie, Cookie::Source::Http);

    if (!url.fragment().is_empty())
        browsing_context().scroll_to_anchor(url.fragment());

    if (auto* host_element = browsing_context().host_element()) {
        // FIXME: Perhaps in the future we'll have a better common base class for <frame> and <iframe>
        VERIFY(is<HTML::HTMLIFrameElement>(*host_element));
        downcast<HTML::HTMLIFrameElement>(*host_element).nested_browsing_context_did_load({});
    }

    if (auto* page = browsing_context().page())
        page->client().page_did_finish_loading(url);
}

void FrameLoader::resource_did_fail()
{
    // FIXME: Don't blindly take value.
    load_error_page(resource()->url().value(), resource()->status_message());
}

}

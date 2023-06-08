#include <AK/NonnullRefPtr.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/VM.h>
#include <LibLine/Editor.h>
#include <LibMain/Main.h>
#include <LibWeb/CSS/CSSRule.h>
#include <LibWeb/CSS/CSSStyleRule.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/Parser/Tokenizer.h>
#include <LibWeb/CSS/Selector.h>
#include <LibWeb/CSS/StyleValues/UnresolvedStyleValue.h>

StringView g_mysql_username;
StringView g_mysql_password;
StringView g_mysql_host = "localhost"sv;
StringView g_mysql_output_file = "/tmp/cssql.out"sv;

enum class ConversionMode {
    Regular,
    Condition,
    Column,
};

struct Name {
    String name;
};

struct NameAndTable {
    String name;
    Variant<Empty, String, NonnullRefPtr<struct Query>, NonnullRefPtr<struct Join>> table {};
};

struct Where {
    Variant<NameAndTable, String> column;
    String op;
    Variant<NameAndTable, String> value;
    bool not_ = false;
    bool and_ = true;
};

struct Join : RefCounted<Join> {
    Join(Variant<String, NonnullRefPtr<struct Query>, NonnullRefPtr<Join>> table_a, Variant<Empty, String, NonnullRefPtr<struct Query>, NonnullRefPtr<Join>> table_b, Vector<NameAndTable> on)
        : table_a(move(table_a))
        , table_b(move(table_b))
        , on(move(on))
    {
    }

    Variant<String, NonnullRefPtr<struct Query>, NonnullRefPtr<Join>> table_a;
    Variant<Empty, String, NonnullRefPtr<struct Query>, NonnullRefPtr<Join>> table_b;
    Vector<NameAndTable> on;
};

ErrorOr<String> perform_join(Join const& join);

template<typename Begin, IteratorPairWith<Begin> End, typename F>
auto map(Begin const& begin, End end, F f)
{
    using Result = decltype(f(*begin));
    Vector<Result> result;
    result.ensure_capacity(end - begin);
    for (auto iter = begin; iter != end; ++iter)
        result.unchecked_append(f(*iter));
    return result;
}

template<typename Iterable, typename F>
auto map(Iterable const& i, F f)
{
    return map(i.begin(), i.end(), move(f));
}

struct Query : RefCounted<Query> {
    Query(Vector<NameAndTable> columns, Variant<String, NonnullRefPtr<Query>, NonnullRefPtr<Join>> table, Vector<Where> where_clause)
        : columns(move(columns))
        , table(move(table))
        , where_clause(move(where_clause))
    {
    }

    Vector<NameAndTable> columns;
    Variant<String, NonnullRefPtr<Query>, NonnullRefPtr<Join>> table;
    Vector<Where> where_clause;

    ErrorOr<String> to_sql(ConversionMode mode = ConversionMode::Regular) const
    {
        StringBuilder builder;
        if (mode == ConversionMode::Regular || mode == ConversionMode::Column) {
            builder.append("select "sv);
            if (columns.is_empty() && table.has<NonnullRefPtr<Join>>())
                builder.append("b.*"sv);
            else if (columns.is_empty())
                builder.append("*"sv);
            else
                builder.join(", "sv, map(columns, [](auto& x) { return x.name; }));
        }
        if (mode == ConversionMode::Regular) {
            builder.append(" from "sv);
            TRY(table.visit(
                [&](String const& name) -> ErrorOr<void> { builder.append(name);  return {}; },
                [&](NonnullRefPtr<Query> const& query) -> ErrorOr<void> { builder.appendff("({}) as T", TRY(query->to_sql())); return {}; },
                [&](NonnullRefPtr<Join> const& join) -> ErrorOr<void> { builder.append(TRY(perform_join(join))); return {}; }));
        }
        if (!where_clause.is_empty() && mode != ConversionMode::Column) {
            builder.append(" where "sv);
            auto first = true;
            for (auto& where : where_clause) {
                if (first)
                    first = false;
                else if (where.and_)
                    builder.append(" and "sv);
                else
                    builder.append(" or "sv);
                if (where.not_)
                    builder.append("not "sv);
                if (auto name = where.column.get_pointer<NameAndTable>()) {
                    if (name->table.has<Empty>())
                        builder.append(name->name);
                    else if (name->table.has<String>())
                        builder.appendff("{}.{}", name->table.get<String>(), name->name);
                    else
                        return Error::from_string_literal("Invalid table name");
                } else {
                    builder.append(where.column.get<String>());
                }
                builder.append(' ');
                builder.append(where.op);
                builder.append(' ');
                if (auto name = where.value.get_pointer<NameAndTable>()) {
                    TRY(name->table.visit(
                        [](Empty) -> ErrorOr<void> { return {}; },
                        [&](String const& s) -> ErrorOr<void> { builder.append(s); return {}; },
                        [&](NonnullRefPtr<Query> const& q) -> ErrorOr<void> { builder.appendff("({})", TRY(q->to_sql())); return {}; },
                        [&](NonnullRefPtr<Join> const& j) -> ErrorOr<void> { builder.appendff("({})", TRY(perform_join(j))); return {}; }));
                    if (!name->table.has<Empty>())
                        builder.append('.');
                    builder.append(name->name);
                } else {
                    builder.append(where.value.get<String>());
                }
            }
        }
        return builder.to_string();
    }
};

ErrorOr<String> perform_join(Join const& join)
{
    // Join{foo, [bar, baz, ...]} -> (foo as a inner join foo as b on a.bar = b.id and a.baz = b.id and ...)
    StringBuilder builder;
    builder.append("("sv);
    TRY(join.table_a.visit(
        [&](String const& name) -> ErrorOr<void> { builder.append(name);  return {}; },
        [&](NonnullRefPtr<struct Query> const& query) -> ErrorOr<void> { builder.appendff("({})", TRY(query->to_sql()));  return {}; },
        [&](NonnullRefPtr<Join> const& join) -> ErrorOr<void> { builder.appendff("({})", TRY(perform_join(join)));  return {}; }));
    builder.append(" as a inner join "sv);
    if (join.table_b.has<Empty>()) {
        TRY(join.table_a.visit(
            [&](String const& name) -> ErrorOr<void> { builder.append(name);  return {}; },
            [&](NonnullRefPtr<struct Query> const& query) -> ErrorOr<void> { builder.appendff("({})", TRY(query->to_sql()));  return {}; },
            [&](NonnullRefPtr<Join> const& join) -> ErrorOr<void> { builder.appendff("({})", TRY(perform_join(join)));  return {}; }));
    } else {
        TRY(join.table_b.visit(
            [&](Empty) -> ErrorOr<void> { return {}; },
            [&](String const& name) -> ErrorOr<void> { builder.append(name);  return {}; },
            [&](NonnullRefPtr<struct Query> const& query) -> ErrorOr<void> { builder.appendff("({})", TRY(query->to_sql()));  return {}; },
            [&](NonnullRefPtr<Join> const& join) -> ErrorOr<void> { builder.appendff("({})", TRY(perform_join(join)));  return {}; }));
    }
    builder.append(" as b on "sv);
    auto first = true;
    for (auto& on : join.on) {
        if (first)
            first = false;
        else
            builder.append(" and "sv);
        TRY(on.table.visit(
            [](Empty) -> ErrorOr<void> { return {}; },
            [&](String const& s) -> ErrorOr<void> { builder.append(s); return {}; },
            [&](NonnullRefPtr<Query> const& q) -> ErrorOr<void> { builder.appendff("({})", TRY(q->to_sql())); return {}; },
            [&](NonnullRefPtr<Join> const& j) -> ErrorOr<void> { builder.appendff("({})", TRY(perform_join(j))); return {}; }));
        if (!on.table.has<Empty>())
            builder.append('.');
        builder.append(on.name);
        builder.append(" = "sv);
        builder.append("b.id"sv);
    }
    builder.append(')');
    return builder.to_string();
}

ErrorOr<NonnullRefPtr<Query>> convert(Web::CSS::Selector const& selector, ConversionMode = ConversionMode::Regular);
ErrorOr<NonnullRefPtr<Query>> convert(Vector<Web::CSS::Selector::SimpleSelector> const& selectors, ConversionMode mode = ConversionMode::Regular)
{
    // [bar op baz] -> (where bar <op> baz)
    // ::is(foo, bar, baz) -> (where foo or bar or baz)
    // ::where(foo, bar, baz) -> (where foo and bar and baz)
    // ::not(foo) -> (where not foo)

    Vector<Where> where_clauses;
    Vector<NameAndTable> selected_columns;
    Variant<Empty, String, NonnullRefPtr<Query>> table;

    for (auto& selector : selectors) {
        switch (selector.type) {
        case Web::CSS::Selector::SimpleSelector::Type::Universal: {
            if (mode == ConversionMode::Regular && table.has<Empty>())
                table = "*"_short_string;
            else
                selected_columns.append({ "*"_short_string });
            break;
        }
        case Web::CSS::Selector::SimpleSelector::Type::TagName: {
            // Tag name -> identifier
            auto name = selector.value.get<Web::CSS::Selector::SimpleSelector::Name>().name.to_string();
            if (mode == ConversionMode::Regular && table.has<Empty>())
                table = name;
            else
                selected_columns.append({ move(name) });
            break;
        }
        case Web::CSS::Selector::SimpleSelector::Type::Id:
            // #foo -> (where id = 'foo')
            where_clauses.empend(NameAndTable { "id"_short_string }, "="_short_string, selector.value.get<Web::CSS::Selector::SimpleSelector::Name>().name.to_string());
            break;
        case Web::CSS::Selector::SimpleSelector::Type::Class:
            // .foo -> (where classes like '%foo%')
            where_clauses.empend(
                NameAndTable { "classes"_short_string },
                "like"_short_string,
                TRY(String::formatted("%{}%", selector.value.get<Web::CSS::Selector::SimpleSelector::Name>().name.to_string())));
            break;
        case Web::CSS::Selector::SimpleSelector::Type::Attribute: {
            // [foo op bar]
            auto& attr = selector.value.get<Web::CSS::Selector::SimpleSelector::Attribute>();
            switch (attr.match_type) {
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::HasAttribute:
                where_clauses.empend(NameAndTable { attr.name.to_string() }, TRY(String::from_utf8("is not"sv)), "null"_short_string);
                break;
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::ExactValueMatch:
                where_clauses.empend(NameAndTable { attr.name.to_string() }, "="_short_string, TRY(String::formatted("'{}'", attr.value)));
                break;
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::ContainsWord:
                where_clauses.empend(
                    NameAndTable { attr.name.to_string() },
                    "like"_short_string,
                    TRY(String::formatted("'% {} %'", attr.value)));
                break;
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::ContainsString:
                where_clauses.empend(
                    NameAndTable { attr.name.to_string() },
                    "like"_short_string,
                    TRY(String::formatted("'%{}%'", attr.value)));
                break;
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::StartsWithSegment:
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::StartsWithString:
                where_clauses.empend(
                    NameAndTable { attr.name.to_string() },
                    "like"_short_string,
                    TRY(String::formatted("'{}%'", attr.value)));
                break;
            case Web::CSS::Selector::SimpleSelector::Attribute::MatchType::EndsWithString:
                where_clauses.empend(
                    NameAndTable { attr.name.to_string() },
                    "like"_short_string,
                    TRY(String::formatted("'%{}'", attr.value)));
                break;
            }
            break;
        }
        case Web::CSS::Selector::SimpleSelector::Type::PseudoClass: {
            // :is, :not, :where
            auto& pseudo_class = selector.value.get<Web::CSS::Selector::SimpleSelector::PseudoClass>();
            switch (pseudo_class.type) {
            case Web::CSS::Selector::SimpleSelector::PseudoClass::Type::Is:
                for (auto& x : pseudo_class.argument_selector_list) {
                    auto query = TRY(convert(x, ConversionMode::Condition));
                    for (auto& c : query->where_clause)
                        c.and_ = false;
                    where_clauses.extend(move(query->where_clause));
                }
                break;
            case Web::CSS::Selector::SimpleSelector::PseudoClass::Type::Where:
                for (auto& x : pseudo_class.argument_selector_list) {
                    auto query = TRY(convert(x, ConversionMode::Condition));
                    where_clauses.extend(move(query->where_clause));
                }
                break;
            case Web::CSS::Selector::SimpleSelector::PseudoClass::Type::Not:
                for (auto& x : pseudo_class.argument_selector_list) {
                    auto query = TRY(convert(x, ConversionMode::Condition));
                    for (auto& c : query->where_clause) {
                        c.and_ = !c.and_;
                        c.not_ = !c.not_;
                    }
                    where_clauses.extend(move(query->where_clause));
                }
                break;
            default:
                return Error::from_string_literal("Unsupported pseudo class");
            }
            break;
        }
        case Web::CSS::Selector::SimpleSelector::Type::PseudoElement:
            return Error::from_string_literal("Unsupported pseudo element");
        }
    }

    switch (mode) {
    case ConversionMode::Regular:
        if (table.has<Empty>())
            table = ""_short_string;
        return make_ref_counted<Query>(
            move(selected_columns),
            move(table).downcast<String, NonnullRefPtr<Query>>(),
            move(where_clauses));
    case ConversionMode::Condition:
        return make_ref_counted<Query>(
            move(selected_columns),
            ""_short_string,
            move(where_clauses));
    case ConversionMode::Column:
        return make_ref_counted<Query>(
            move(selected_columns),
            ""_short_string,
            Vector<Where> {});
    }
}

ErrorOr<NonnullRefPtr<Query>> convert(Web::CSS::Selector const& selector, ConversionMode mode)
{
    // foo -> (from foo)
    // <x> <y> -> (select <y> from (<x>))
    // foo > bar -> (select bar from foo)
    // foo + bar -> (select ..., bar from foo)
    // foo ~ bar [where foo is a join] -> [set join's target table]

    if (mode == ConversionMode::Condition) {
        auto& compound_selector = selector.compound_selectors().last();
        return convert(compound_selector.simple_selectors, mode); // FIXME: Don't ignore combinator
    }

    if (mode == ConversionMode::Column) {
        Vector<NameAndTable> columns;
        for (auto& compound_selector : selector.compound_selectors())
            columns.extend(TRY(convert(compound_selector.simple_selectors, mode))->columns);

        return make_ref_counted<Query>(move(columns), ""_short_string, Vector<Where> {});
    }

    RefPtr<Query> query;
    for (auto& compound_selector : selector.compound_selectors()) {
        if (!query) {
            query = TRY(convert(compound_selector.simple_selectors, mode));
            continue;
        }

        switch (compound_selector.combinator) {
        case Web::CSS::Selector::Combinator::None:
            return Error::from_string_literal("Multiple compound selectors not allowed");
        case Web::CSS::Selector::Combinator::Descendant: {
            auto q = TRY(convert(compound_selector.simple_selectors, ConversionMode::Regular));
            if (auto s = q->table.get_pointer<String>()) {
                if (s->is_empty()) {
                    q->table = query.release_nonnull();
                    query = move(q);
                } else {
                    query->columns.append({ *s });
                    query->where_clause.extend(move(q->where_clause));
                }
            } else {
                return Error::from_string_literal("Descendant subquery must not be a full query");
            }
            break;
        }
        case Web::CSS::Selector::Combinator::ImmediateChild: {
            // foo > bar > baz -> (select baz from foo as a inner join foo as b on a.bar = b.id)
            if (query->columns.is_empty()) {
                auto cols = TRY(convert(compound_selector.simple_selectors, ConversionMode::Column));
                auto conditions = TRY(convert(compound_selector.simple_selectors, ConversionMode::Condition));
                // query->columns.extend(move(cols->columns));
                query = make_ref_counted<Query>(
                    move(cols->columns),
                    query.release_nonnull(),
                    move(conditions->where_clause));
            } else {
                Variant<Empty, String, NonnullRefPtr<struct Query>, NonnullRefPtr<struct Join>> reference_table;
                for (auto& column : query->columns) {
                    if (!column.table.has<Empty>()) {
                        if (reference_table.has<Empty>())
                            reference_table = column.table;
                        else
                            return Error::from_string_literal("Multiple tables in immediate child selector");
                    }
                    column.table = "a"_short_string;
                }

                for (auto& where : query->where_clause)
                    if (auto name = where.column.get_pointer<NameAndTable>())
                        name->table = "a"_short_string;

                query->table = make_ref_counted<Join>(
                    move(query->table),
                    move(reference_table),
                    move(query->columns));
                query->columns = {};

                auto q = TRY(convert(compound_selector.simple_selectors, ConversionMode::Regular));
                if (auto s = q->table.get_pointer<String>()) {
                    if (!s->is_empty())
                        q->columns.append({ *s });
                    q->table = query.release_nonnull();
                    query = move(q);
                } else {
                    return Error::from_string_literal("Immediate subquery must not be a full query");
                }
            }
            break;
        }
        case Web::CSS::Selector::Combinator::NextSibling: {
            auto cols = TRY(convert(compound_selector.simple_selectors, ConversionMode::Column));
            query->columns.extend(move(cols->columns));
            break;
        }
        case Web::CSS::Selector::Combinator::SubsequentSibling: {
            auto q = TRY(convert(compound_selector.simple_selectors, ConversionMode::Regular));
            if (query->columns.is_empty()) {
                warnln("Subsequent sibling query must have columns");
                return Error::from_string_literal("Subsequent sibling query must have columns");
            }
            if (query->columns.last().table.has<Empty>()) {
                query->columns.last().table = move(q);
            } else {
                warnln("Declared column {} already has a reference table", query->columns.last().name);
                return Error::from_string_literal("Declared column already has a reference table");
            }
            break;
        }
        case Web::CSS::Selector::Combinator::Column:
            return Error::from_string_literal("Unsupported combinator");
        }
    }

    if (!query)
        return Error::from_string_literal("Empty selector");

    return query.release_nonnull();
}

DeprecatedString run_sql(DeprecatedString s)
{
    auto c = DeprecatedString::formatted(
        "python -c '"
        "import mysql.connector;"
        "x=mysql.connector.connect(user=\"{}\", password=\"{}\", host=\"{}\", database=\"serenity\");"
        "c=x.cursor();"
        "c.execute(input());"
        "print(list(c));"
        "x.commit();"
        "' > {}",
        g_mysql_username,
        g_mysql_password,
        g_mysql_host,
        g_mysql_output_file);

    auto fp = popen(c.characters(), "w");
    if (!fp) {
        warnln("Failed to open pipe to mysql");
        return "";
    }

    fputs(s.characters(), fp);
    fputs("\n", fp);
    fflush(fp);
    pclose(fp);

    auto result = Core::File::open(g_mysql_output_file, Core::File::OpenMode::Read);
    if (result.is_error()) {
        warnln("Failed to open mysql output file: {}", result.error());
        return "";
    }

    auto file = result.release_value();
    auto buffer = file->read_until_eof();
    if (buffer.is_error()) {
        warnln("Failed to read mysql output file: {}", buffer.error());
        return "";
    }

    return DeprecatedString::copy(buffer.value());
}

struct Key {
    String table;
    String column;
};

struct ResolvedValue {
    String value;
    struct Flags {
        bool nullable { true };
        bool unique { false };
        bool primary_key { false };
        bool auto_increment { false };
        Optional<Key> foreign_key {};
    } flags {};
};

ErrorOr<ResolvedValue> resolve(NonnullRefPtr<Web::CSS::StyleValue const> const& value)
{
    ResolvedValue resolved;
    resolved.value = TRY([&]() -> ErrorOr<String> {
        if (value->is_unresolved()) {
            auto& u = value->as_unresolved();
            if (u.values().size() < 1) {
                warnln("Error: value must have at least one component");
                return Error::from_string_literal("Invalid value");
            }
            auto& component = u.values().first();
            if (component.is(Web::CSS::Parser::Token::Type::Ident))
                return String::formatted("\"{}\"", TRY(component.to_string()));
            if (component.is(Web::CSS::Parser::Token::Type::Number) || component.is(Web::CSS::Parser::Token::Type::String))
                return component.to_string();

            warnln("Error: value must be a string, number, or ident");
            return Error::from_string_literal("Invalid value");
        }
        if (value->is_number())
            return value->to_string();
        if (value->is_string())
            return String::formatted("\"{}\"", TRY(value->to_string()));

        warnln("Error: value must be a string, number, or ident");
        return Error::from_string_literal("Invalid value");
    }());

    if (value->is_unresolved()) {
        auto& u = value->as_unresolved();
        for (size_t i = 1; i < u.values().size(); ++i) {
            auto& v = u.values()[i];
            if (v.is_block()) {
                warnln("Error: value must not have blocks or functions");
                return Error::from_string_literal("Invalid value");
            }

            if (v.is_function()) {
                auto& fn = v.function();
                if (fn.name() != "foreign-key"sv) {
                    warnln("Error: unknown attribute '{}'", fn.name());
                    return Error::from_string_literal("Unknown function");
                }

                Web::CSS::Parser::TokenStream stream { fn.values() };
                stream.skip_whitespace();

                auto& table = stream.next_token();
                stream.skip_whitespace();
                if (!stream.next_token().is(Web::CSS::Parser::Token::Type::Comma)) {
                    warnln("Error: foreign-key must have two arguments");
                    return Error::from_string_literal("Invalid function");
                }
                stream.skip_whitespace();
                auto& column = stream.next_token();
                stream.skip_whitespace();
                if (stream.has_next_token()) {
                    warnln("Error: foreign-key must have two arguments");
                    return Error::from_string_literal("Invalid function");
                }

                if (!table.is(Web::CSS::Parser::Token::Type::Ident)) {
                    warnln("Error: foreign-key table must be an ident");
                    return Error::from_string_literal("Invalid function");
                }

                if (!column.is(Web::CSS::Parser::Token::Type::Ident)) {
                    warnln("Error: foreign-key column must be an ident");
                    return Error::from_string_literal("Invalid function");
                }

                resolved.flags.foreign_key = Key {
                    TRY(String::from_utf8(TRY(table.to_string()).bytes_as_string_view().to_lowercase_string())),
                    TRY(column.to_string()),
                };
                continue;
            }

            if (v.token().is(Web::CSS::Parser::Token::Type::Ident)) {
                auto str = TRY(v.to_string());
                if (str == "nullable"sv) {
                    resolved.flags.nullable = true;
                } else if (str == "not-null"sv) {
                    resolved.flags.nullable = false;
                } else if (str == "unique"sv) {
                    resolved.flags.unique = true;
                } else if (str == "primary-key"sv) {
                    resolved.flags.primary_key = true;
                } else if (str == "auto-increment"sv) {
                    resolved.flags.auto_increment = true;
                } else {
                    warnln("Error: unknown flag '{}'", str);
                    return Error::from_string_literal("Unknown value");
                }
            } else if (!v.token().is(Web::CSS::Parser::Token::Type::Whitespace)) {
                warnln("Error: value must be a string, number, or ident");
                return Error::from_string_literal("Invalid value");
            }
        }
    }

    return resolved;
}

ErrorOr<String> type_name(ResolvedValue const& name)
{
    auto type = TRY([&]() -> ErrorOr<StringView> {
        if (name.value.bytes_as_string_view().equals_ignoring_ascii_case("\"int\""sv))
            return "int"sv;
        if (name.value.bytes_as_string_view().equals_ignoring_ascii_case("\"string\""sv))
            return "varchar(255)"sv;

        warnln("Error: unknown type {}", name.value);
        return Error::from_string_literal("Unknown type");
    }());

    if (name.flags.primary_key && type != "int"sv) {
        warnln("Error: primary key must be an int");
        return Error::from_string_literal("Invalid type");
    }

    if (name.flags.auto_increment && type != "int"sv) {
        warnln("Error: auto increment must be an int");
        return Error::from_string_literal("Invalid type");
    }

    StringBuilder builder;
    builder.append(type);
    if (name.flags.auto_increment)
        builder.append(" auto_increment"sv);
    if (name.flags.unique)
        builder.append(" unique"sv);
    if (!name.flags.nullable)
        builder.append(" not null"sv);
    return builder.to_string();
}

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    Core::ArgsParser args_parser;
    args_parser.add_option(g_mysql_username, "MySQL username", "mysql-username", 'u', "username");
    args_parser.add_option(g_mysql_password, "MySQL password", "mysql-password", 'p', "password");
    args_parser.add_option(g_mysql_host, "MySQL host", "mysql-host", 'h', "host");
    args_parser.add_option(g_mysql_output_file, "Temporary output file", "temp-file", 't', "path");

    args_parser.parse(arguments);

    Core::EventLoop loop;

    auto vm = TRY(JS::VM::create());
    auto realm = MUST(JS::Realm::create(*vm));
    auto context = Web::CSS::Parser::ParsingContext(*realm);
    auto editor = TRY(Line::Editor::try_create());
    editor->on_display_refresh = [&](auto&) -> void {
        editor->strip_styles();
        auto line = editor->line();
        line = line.replace("\n"sv, " "sv, AK::ReplaceMode::All);

#define TRY_OR_IGNORE(x)    \
    ({                      \
        auto _t = x;        \
        if (_t.is_error())  \
            return;         \
        _t.release_value(); \
    })

        auto tokens = TRY_OR_IGNORE(Web::CSS::Parser::Tokenizer::tokenize(line, "utf-8"sv));
        for (auto& token : tokens) {
            Line::Span const span { token.start_position().column, token.end_position().column };
            Line::Style style;
            switch (token.type()) {
            case Web::CSS::Parser::Token::Type::Invalid:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Red));
                break;
            case Web::CSS::Parser::Token::Type::Ident:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Blue));
                break;
            case Web::CSS::Parser::Token::Type::Function:
            case Web::CSS::Parser::Token::Type::OpenParen:
            case Web::CSS::Parser::Token::Type::CloseParen:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Magenta));
                break;
            case Web::CSS::Parser::Token::Type::AtKeyword:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Cyan));
                break;
            case Web::CSS::Parser::Token::Type::Hash:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Yellow));
                break;
            case Web::CSS::Parser::Token::Type::BadString:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Green));
                style.set(Line::Style::Underline);
                break;
            case Web::CSS::Parser::Token::Type::String:
            case Web::CSS::Parser::Token::Type::Number:
            case Web::CSS::Parser::Token::Type::Percentage:
            case Web::CSS::Parser::Token::Type::Dimension:
            case Web::CSS::Parser::Token::Type::Url:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Green));
                break;
            case Web::CSS::Parser::Token::Type::BadUrl:
                style.set(Line::Style::Foreground(Line::Style::XtermColor::Green));
                style.set(Line::Style::Underline);
                break;
            case Web::CSS::Parser::Token::Type::EndOfFile:
            case Web::CSS::Parser::Token::Type::Delim:
            case Web::CSS::Parser::Token::Type::Whitespace:
            case Web::CSS::Parser::Token::Type::CDO:
            case Web::CSS::Parser::Token::Type::CDC:
            case Web::CSS::Parser::Token::Type::Colon:
            case Web::CSS::Parser::Token::Type::Semicolon:
            case Web::CSS::Parser::Token::Type::Comma:
            case Web::CSS::Parser::Token::Type::OpenSquare:
            case Web::CSS::Parser::Token::Type::CloseSquare:
            case Web::CSS::Parser::Token::Type::OpenCurly:
            case Web::CSS::Parser::Token::Type::CloseCurly:
                break;
            }

            if (!style.is_empty())
                editor->stylize(span, style);
        }

#undef TRY_OR_IGNORE
    };
    while (true) {
        auto result = editor->get_line("css> ");
        if (result.is_error()) {
            if (result.error() == Line::Editor::Error::Empty)
                continue;
            break;
        }

        editor->add_to_history(result.value());

        auto parser = TRY(Web::CSS::Parser::Parser::create(context, result.value()));
        auto selectors = parser.parse_as_selector(Web::CSS::Parser::Parser::SelectorParsingMode::Standard);
        Web::CSS::PropertyOwningCSSStyleDeclaration const* declaration = nullptr;
        if (!selectors.has_value()) {
            auto rule_parser = TRY(Web::CSS::Parser::Parser::create(context, result.value()));
            auto rule = rule_parser.parse_as_css_rule();
            if (!rule || !is<Web::CSS::CSSStyleRule>(rule)) {
                warnln("Failed to parse '{}' as a CSS selector or rule", result.value());
                continue;
            }

            auto style_rule = static_cast<Web::CSS::CSSStyleRule const*>(rule);

            selectors = style_rule->selectors();
            declaration = verify_cast<Web::CSS::PropertyOwningCSSStyleDeclaration>(&style_rule->declaration());
        }

        for (auto& selector : *selectors) {
            auto query = convert(selector);
            if (query.is_error()) {
                warnln("Failed to convert CSS selector to SQL: {}", query.error());
                continue;
            }

            auto resolved_query = MUST(move(query));
            if (declaration) {
                // This is either an insert, or an update.
                if (!resolved_query->table.has<String>()) {
                    warnln("Error: DDL requires a table name, 'insert table', 'delete table', or '* table' only");
                    continue;
                }

                auto& table = resolved_query->table.get<String>();
                if (table == "*"sv) {
                    // Definition mode.
                    auto& table_name = resolved_query->columns.first();
                    StringBuilder builder;
                    builder.appendff("create table {}(", table_name.name);
                    auto first = true;
                    for (auto& entry : declaration->custom_properties()) {
                        if (first)
                            first = false;
                        else
                            builder.append(", "sv);
                        builder.append(entry.key.substring_view(2));
                        builder.appendff(" {}"sv, TRY(type_name(TRY(resolve(entry.value.value)))));
                    }
                    for (auto& entry : declaration->custom_properties()) {
                        auto need_comma = !first;
                        auto v = TRY(resolve(entry.value.value));
                        if (v.flags.primary_key) {
                            if (need_comma)
                                builder.append(", "sv);
                            builder.appendff("primary key({})", entry.key.substring_view(2));
                            need_comma = true;
                        }
                        if (v.flags.foreign_key.has_value()) {
                            if (need_comma)
                                builder.append(", "sv);
                            builder.appendff("foreign key({}) references {}({})", entry.key.substring_view(2), v.flags.foreign_key->table, v.flags.foreign_key->column);
                        }
                        first = !need_comma;
                    }
                    builder.append(")"sv);

                    outln("\x1b[33m- {} -> {}\x1b[0m", selector, builder.string_view());

                    outln("{}", run_sql(builder.string_view()));
                    continue;
                }

                if (table == "insert"sv) {
                    // Insert mode.
                    auto& table_to_insert_to = resolved_query->columns.first();
                    StringBuilder builder;
                    builder.appendff("insert into {}(", table_to_insert_to.name);
                    auto first = true;
                    for (auto& entry : declaration->custom_properties()) {
                        if (first)
                            first = false;
                        else
                            builder.append(", "sv);
                        builder.append(entry.key.substring_view(2));
                    }
                    builder.append(") values("sv);
                    first = true;
                    for (auto& entry : declaration->custom_properties()) {
                        if (first)
                            first = false;
                        else
                            builder.append(", "sv);

                        builder.append(TRY(resolve(entry.value.value)).value);
                    }
                    builder.append(")"sv);

                    outln("\x1b[33m- {} -> {}\x1b[0m", selector, builder.string_view());

                    outln("{}", run_sql(builder.string_view()));
                    continue;
                }

                if (table == "delete"sv) {
                    // Delete mode.
                    if (resolved_query->columns.size() != 1) {
                        warnln("Error: delete requires exactly one column");
                        continue;
                    }
                    auto& column_to_delete_from = resolved_query->columns.first();
                    StringBuilder builder;
                    builder.appendff("delete from {}", column_to_delete_from.name);
                    builder.append(TRY(TRY(convert(selector, ConversionMode::Condition))->to_sql(ConversionMode::Condition)));

                    outln("\x1b[33m- {} -> {}\x1b[0m", selector, builder.string_view());

                    outln("{}", run_sql(builder.string_view()));
                    continue;
                }

                // Update mode.
                StringBuilder builder;
                builder.appendff("update {} set ", table);
                auto first = true;
                for (auto& entry : declaration->custom_properties()) {
                    if (first)
                        first = false;
                    else
                        builder.append(", "sv);
                    builder.appendff("{} = ", entry.key.substring_view(2));
                    builder.append(TRY(resolve(entry.value.value)).value);
                }
                builder.append(TRY(TRY(convert(selector, ConversionMode::Condition))->to_sql(ConversionMode::Condition)));

                outln("\x1b[33m- {} -> {}\x1b[0m", selector, builder.string_view());

                outln("{}", run_sql(builder.string_view()));
                continue;
            }

            auto sql = TRY(resolved_query->to_sql());
            outln("\x1b[33m- {} -> {}\x1b[0m", selector, sql);
            outln("{}", run_sql(sql.to_deprecated_string()));
        }
    }

    return 0;
}

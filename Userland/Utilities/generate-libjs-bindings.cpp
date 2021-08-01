/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <AK/MappedFile.h>
#include <AK/Queue.h>
#include <AK/SourceGenerator.h>
#include <AK/Tuple.h>
#include <AK/Variant.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibCpp/Parser.h>
#include <LibCpp/Preprocessor.h>

Vector<String> g_include_paths;

using OverloadSet = Vector<Cpp::FunctionDeclaration const*>;
using ClassContents = Vector<Variant<OverloadSet, Cpp::VariableDeclaration const*>>;
using RootClass = HashMap<Cpp::StructOrClassDeclaration const*, ClassContents>;
using NSContents = Vector<Variant<OverloadSet, Cpp::VariableDeclaration const*, RootClass>>;
using RootNS = HashMap<Cpp::NamespaceDeclaration const*, NSContents>;

enum class GenerationTarget {
    StaticMembers,
    InstanceMembers,
    InitializeStaticMembers,
    InitializeInstanceMembers,
};

static void discover_toplevel(Cpp::Declaration& declaration, RootNS& ns);
static void discover_ns(Cpp::Declaration& declaration, NSContents& contents, RootNS& ns);

static String find_path(StringView path)
{
    LexicalPath lexical_path { path };
    if (lexical_path.is_absolute())
        return path;

    for (auto& search_dir : g_include_paths) {
        auto actual_path = LexicalPath::join(search_dir, path);
        if (Core::File::exists(actual_path.string()))
            return actual_path.string();
    }

    return path;
}

static int discover(StringView path, RootNS& ns, Queue<String>& includes, HashTable<String>& already_included, Vector<Tuple<String, NonnullRefPtr<Cpp::TranslationUnit>>>& units)
{
    String contents;
    {
        auto file = MappedFile::map(path);
        if (file.is_error()) {
            warnln("Opening {} failed: {}", path, file.error().string());
            return 1;
        }
        auto pp = Cpp::Preprocessor { path, StringView { file.value()->bytes() } };
        contents = pp.process();
        for (auto& entry : pp.included_paths()) {
            if (entry.starts_with('<')) {
                auto include_path = entry.substring_view(1, entry.length() - 2);
                auto path = find_path(include_path);
                if (already_included.contains(path))
                    continue;
                already_included.set(path);
                includes.enqueue(path);
            } else {
                dbgln("Not including {}", entry);
            }
        }
    }

    Cpp::Parser parser { contents, path };
    auto tu = parser.parse();
    units.empend(contents, tu);

    for (auto& decl : tu->declarations())
        discover_toplevel(decl, ns);

    return 0;
}

static void discover_class(Cpp::Declaration& declaration, ClassContents& contents, NSContents& ns_contents, RootNS& ns)
{
    if (declaration.is_struct_or_class())
        return discover_ns(declaration, ns_contents, ns);

    if (declaration.is_variable_declaration()) {
        contents.append(&static_cast<Cpp::VariableDeclaration&>(declaration));
        return;
    }

    if (declaration.is_function()) {
        auto& fn = static_cast<Cpp::FunctionDeclaration&>(declaration);
        auto it = contents.find_if([name = fn.name()](auto& entry) {
            auto set = entry.template get_pointer<OverloadSet>();
            return set && set->first()->name() == name;
        });
        if (it == contents.end()) {
            contents.append(OverloadSet { &fn });
        } else {
            it->get<OverloadSet>().append(&fn);
        }
        return;
    }

    dbgln("Ignoring entry '{}' in class '{}'", declaration.class_name(), static_cast<Cpp::StructOrClassDeclaration const*>(declaration.parent())->name());
}

void discover_ns(Cpp::Declaration& declaration, NSContents& contents, RootNS& ns)
{
    if (declaration.is_namespace())
        return discover_toplevel(declaration, ns);

    if (declaration.is_struct_or_class()) {
        auto& struct_or_class = static_cast<Cpp::StructOrClassDeclaration&>(declaration);
        auto it = contents.find_if([](auto& x) { return x.template has<RootClass>(); });
        if (!(it != contents.end())) {
            contents.append(RootClass {});
            it = contents.end() - 1;
        }

        auto& root_class = it->get<RootClass>();
        ClassContents* class_contents = nullptr;
        if (auto it = root_class.find(&struct_or_class); it == root_class.end()) {
            root_class.set(&struct_or_class, {});
            class_contents = &root_class.find(&struct_or_class)->value;
        } else {
            class_contents = &it->value;
        }
        VERIFY(class_contents);
        for (auto& decl : struct_or_class.declarations())
            discover_class(decl, *class_contents, contents, ns);
        return;
    }

    if (declaration.is_variable_declaration()) {
        contents.append(&static_cast<Cpp::VariableDeclaration&>(declaration));
        return;
    }

    if (declaration.is_function()) {
        auto& fn = static_cast<Cpp::FunctionDeclaration&>(declaration);
        auto it = contents.find_if([name = fn.name()](auto& entry) {
            auto set = entry.template get_pointer<OverloadSet>();
            return set && set->first()->name() == name;
        });
        if (it == contents.end()) {
            contents.append(OverloadSet { &fn });
        } else {
            it->get<OverloadSet>().append(&fn);
        }
        return;
    }

    dbgln("Ignored unknown entity '{}'", declaration.class_name());
}

static auto ns_qualified_name(Cpp::NamespaceDeclaration const& ns_decl)
{
    auto name = String::formatted("::{}", ns_decl.name());
    for (auto ptr = ns_decl.parent(); ptr; ptr = ptr->parent()) {
        if (!ptr->is_declaration())
            break;
        auto& decl = static_cast<Cpp::Declaration const&>(*ptr);
        if (!decl.is_namespace())
            break;
        auto& ns = static_cast<Cpp::NamespaceDeclaration const&>(decl);
        name = String::formatted("{}::{}", ns.name(), name);
    }

    return name;
};

void discover_toplevel(Cpp::Declaration& declaration, RootNS& ns)
{
    if (declaration.is_namespace()) {
        auto& namespace_decl = static_cast<Cpp::NamespaceDeclaration&>(declaration);
        NSContents* contents = nullptr;
        if (auto it = ns.find(&namespace_decl); it != ns.end()) {
            contents = &it->value;
        } else {
            bool found = false;
            auto qn = ns_qualified_name(namespace_decl);
            for (auto& entry : ns) {
                if (entry.key && ns_qualified_name(*entry.key) == qn) {
                    contents = &entry.value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ns.set(&namespace_decl, {});
                contents = &ns.find(&namespace_decl)->value;
            }
        }
        VERIFY(contents);

        for (auto& entry : declaration.declarations())
            discover_ns(entry, *contents, ns);
    } else {
        NSContents* contents = nullptr;
        if (auto it = ns.find(nullptr); it != ns.end()) {
            contents = &it->value;
        } else {
            ns.set(nullptr, {});
            contents = &ns.find(nullptr)->value;
        }
        VERIFY(contents);

        for (auto& entry : declaration.declarations())
            discover_ns(entry, *contents, ns);
    }
}

static auto find_ns(Cpp::NamespaceDeclaration const& decl, RootNS const& ns) -> NSContents const&
{
    auto name = ns_qualified_name(decl);
    for (auto& entry : ns) {
        if (entry.key == &decl)
            return entry.value;
        if (entry.key == nullptr)
            continue;
        if (ns_qualified_name(*entry.key) == name)
            return entry.value;
    }
    VERIFY_NOT_REACHED();
}

static auto resolve_class(Cpp::StructOrClassDeclaration& decl, RootNS const& ns) -> ClassContents const&
{
    Cpp::NamespaceDeclaration const* ns_ptr = nullptr;
    for (Cpp::ASTNode const* ptr = &decl; ptr; ptr = ptr->parent()) {
        if (is<Cpp::NamespaceDeclaration>(ptr)) {
            ns_ptr = static_cast<Cpp::NamespaceDeclaration const*>(ptr);
            break;
        }
    }
    auto& ns_contents = find_ns(*ns_ptr, ns);
    auto class_it = ns_contents.find_if([&](auto& entry) { return entry.template has<RootClass>() && entry.template get<RootClass>().contains(&decl); });
    VERIFY(class_it != ns_contents.end());
    return class_it->get<RootClass>().find(&decl)->value;
}

struct NumericTypeProperties {
    size_t bit_width { 0 };
    bool is_signed { false };
    bool is_floating { false };
};

static Optional<NumericTypeProperties> numeric_type_properties(Cpp::Type const& type)
{
    if (type.is_auto())
        return {};

    if (!is<Cpp::NamedType>(type))
        return {};

    auto& named_type = static_cast<Cpp::NamedType const&>(type);
    if (named_type.is_templatized())
        return {};

    if (!named_type.name() || !named_type.name()->name())
        return {};

    auto& name = named_type.name()->name()->name();
    if (name == "bool"sv)
        return NumericTypeProperties { 1, false };

    if (name.is_one_of("signed char"sv, "char"sv, "i8"sv))
        return NumericTypeProperties { 8, true };
    if (name.is_one_of("unsigned char"sv, "u8"sv))
        return NumericTypeProperties { 8, false };

    if (name.is_one_of("short"sv, "i16"sv))
        return NumericTypeProperties { 16, true };
    if (name.is_one_of("unsigned short"sv, "u16"sv))
        return NumericTypeProperties { 16, false };

    constexpr bool long_is_32_bits = sizeof(long) == sizeof(int);

    if constexpr (long_is_32_bits) {
        if (name == "long"sv)
            return NumericTypeProperties { 32, true };
        if (name == "unsigned long"sv)
            return NumericTypeProperties { 32, false };
    }

    if (name.is_one_of("int"sv, "i32"sv))
        return NumericTypeProperties { 32, true };
    if (name.is_one_of("unsigned int"sv, "unsigned"sv, "u32"sv))
        return NumericTypeProperties { 32, false };

    if constexpr (!long_is_32_bits) {
        if (name == "long"sv)
            return NumericTypeProperties { 64, true };
        if (name == "unsigned long"sv)
            return NumericTypeProperties { 64, false };
    }

    if (name.is_one_of("long long"sv, "i64"sv))
        return NumericTypeProperties { 64, true };
    if (name.is_one_of("unsigned long long"sv, "u64"sv))
        return NumericTypeProperties { 64, false };

    if (name == "float"sv)
        return NumericTypeProperties { 32, true, true };

    if (name == "double"sv)
        return NumericTypeProperties { 64, true, true };

    if (name == "long double"sv)
        return NumericTypeProperties { 80, true, true };

    return {};
}

struct NonNumericTypeProperties {
    StringView name;
};

static Optional<NonNumericTypeProperties> non_numeric_type_properties(Cpp::Type const& type)
{
    if (type.is_auto())
        return {};

    if (!is<Cpp::NamedType>(type))
        return {};

    auto& named_type = static_cast<Cpp::NamedType const&>(type);
    if (named_type.is_templatized())
        return {};

    if (!named_type.name() || !named_type.name()->name())
        return {};

    return NonNumericTypeProperties { named_type.name()->name()->name() };
}

static Optional<String> convert_to_js_value(StringView expr, Cpp::Type const& type)
{
    if (type.is_auto())
        return {};

    if (!is<Cpp::NamedType>(type))
        return {};

    auto& named_type = static_cast<Cpp::NamedType const&>(type);
    if (named_type.is_templatized())
        return {};

    if (numeric_type_properties(type).has_value())
        return String::formatted("JS::Value({})", expr);

    if (named_type.name()->name()->name().is_one_of("String"sv, "StringView"sv))
        return String::formatted("JS::js_string(vm.heap(), {})", expr);

    if (named_type.name()->name()->name() == "void"sv)
        return String::formatted("[&] {{ {}; return JS::js_null(); }}()", expr);

    return {};
}

static Optional<String> convert_from_js_value(StringView expr, Cpp::Type const& type)
{
    if (auto maybe_properties = numeric_type_properties(type); maybe_properties.has_value()) {
        auto& properties = *maybe_properties;
        if (properties.is_floating)
            return String::formatted("({}).to_double(global_object)", expr);

        if (properties.bit_width == 1)
            return String::formatted("(({}).to_double(global_object) != 0)", expr);

        if (properties.bit_width <= 32)
            return String::formatted("static_cast<{}{}>(({}).to_double(global_object))", properties.is_signed ? 'i' : 'u', properties.bit_width, expr);
        VERIFY(properties.bit_width == 64);
        return String::formatted("({}).to_bigint_{}64(global_object))", properties.is_signed ? 'i' : 'u', expr);
    }

    if (auto maybe_properties = non_numeric_type_properties(type); maybe_properties.has_value()) {
        auto& properties = *maybe_properties;
        if (properties.name.is_one_of("String"sv, "StringView"sv))
            return String::formatted("{} {{ ({}).to_string(global_object) }}", properties.name, expr);

        if (properties.name == "void"sv)
            return String::formatted("(void)({})", expr);
    }

    return {};
}

static void generate(Cpp::Declaration& declaration, RootNS const& ns, SourceGenerator& generator, GenerationTarget target = GenerationTarget::StaticMembers)
{
    if (declaration.is_namespace()) {
        if (target != GenerationTarget::StaticMembers)
            return;

        auto ns_generator = generator.fork();
        ns_generator.set("namespace", declaration.name());
        ns_generator.set("namespace.object", String::formatted("{}Object", declaration.name()));

        for (auto& entry : find_ns(static_cast<Cpp::NamespaceDeclaration&>(declaration), ns)) {
            entry.visit(
                [&](OverloadSet const& overload) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*overload.first())),
                        ns, ns_generator, GenerationTarget::StaticMembers);
                },
                [&](auto* ptr) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*ptr)),
                        ns, ns_generator, GenerationTarget::StaticMembers);
                },
                [&](RootClass const& root_class) {
                    for (auto& class_ : root_class) {
                        generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*class_.key)),
                            ns, ns_generator, GenerationTarget::StaticMembers);
                    }
                });
        }

        ns_generator.append(R"~~(
class @namespace.object@ final : public JS::Object {
    JS_OBJECT(@namespace.object@, JS::Object);

public:
    explicit @namespace.object@(JS::GlobalObject& global_object)
        : Object(*global_object.object_prototype())
    {
    }

    virtual ~@namespace.object@() override = default;
    virtual void initialize(JS::GlobalObject& global_object) override
    {
        Base::initialize(global_object);
        [[maybe_unused]] auto& vm = global_object.vm();
)~~");

        for (auto& entry : find_ns(static_cast<Cpp::NamespaceDeclaration&>(declaration), ns)) {
            entry.visit(
                [&](OverloadSet const& overload) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*overload.first())),
                        ns, ns_generator, GenerationTarget::InitializeStaticMembers);
                },
                [&](auto* ptr) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*ptr)),
                        ns, ns_generator, GenerationTarget::InitializeStaticMembers);
                },
                [&](RootClass const& root_class) {
                    auto class_generator = ns_generator.fork();
                    for (auto& class_ : root_class) {
                        class_generator.set("class.name", class_.key->name());
                        class_generator.set("class.name.object", String::formatted("{}InstanceObject", class_.key->name()));
                        class_generator.append(R"~~(
        define_direct_property("@class.name@", vm.heap().allocate<@class.name.object@>(global_object, global_object), JS::Attribute::Enumerable);
)~~");
                    }
                });
        }

        ns_generator.append(R"~~(
    }
private:
)~~");
        for (auto& entry : find_ns(static_cast<Cpp::NamespaceDeclaration&>(declaration), ns)) {
            if (auto ptr = entry.get_pointer<Cpp::VariableDeclaration const*>()) {
                generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(**ptr)),
                    ns, ns_generator, GenerationTarget::InstanceMembers);
            }
        }

        ns_generator.append(R"~~(
};
)~~");
    } else if (declaration.is_struct_or_class()) {
        if (target != GenerationTarget::StaticMembers)
            return;

        auto class_generator = generator.fork();
        class_generator.set("class", declaration.name());
        String name = String::formatted("::{}", declaration.name());
        for (auto ptr = declaration.parent(); ptr; ptr = ptr->parent()) {
            if (!ptr->is_declaration())
                break;
            name = String::formatted("::{}{}", static_cast<Cpp::Declaration const*>(ptr)->name(), name);
        }
        class_generator.set("class.fqn", name);
        class_generator.set("class.object", String::formatted("{}InstanceObject", declaration.name()));

        class_generator.append(R"~~(
class @class.object@ final : public JS::Object {
    JS_OBJECT(@class.object@, JS::Object);

public:
    explicit @class.object@(JS::GlobalObject& global_object, @class.fqn@& instance)
        : Object(*global_object.object_prototype())
        , m_instance(&instance)
    {
    }

    explicit @class.object@(JS::GlobalObject& global_object)
        : Object(*global_object.object_prototype())
        , m_instance(nullptr)
    {
    }

    virtual ~@class.object@() override = default;

    virtual void initialize(JS::GlobalObject& global_object) override
    {
        Base::initialize(global_object);
        [[maybe_unused]] auto& vm = global_object.vm();
)~~");
        auto& class_ = resolve_class(static_cast<Cpp::StructOrClassDeclaration&>(declaration), ns);
        for (auto& entry : class_) {
            entry.visit(
                [&](OverloadSet const& overload) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*overload.first())),
                        ns, class_generator, GenerationTarget::InitializeStaticMembers);
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*overload.first())),
                        ns, class_generator, GenerationTarget::InitializeInstanceMembers);
                },
                [&](auto* ptr) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*ptr)),
                        ns, class_generator, GenerationTarget::InitializeStaticMembers);
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*ptr)),
                        ns, class_generator, GenerationTarget::InitializeInstanceMembers);
                });
        }

        class_generator.append(R"~~(
    }

private:
)~~");

        for (auto& entry : class_) {
            entry.visit(
                [&](OverloadSet const& overload) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*overload.first())),
                        ns, class_generator, GenerationTarget::InstanceMembers);
                },
                [&](auto* ptr) {
                    generate(const_cast<Cpp::Declaration&>(static_cast<Cpp::Declaration const&>(*ptr)),
                        ns, class_generator, GenerationTarget::InstanceMembers);
                });
        }

        class_generator.append(R"~~(
    @class.fqn@* m_instance;
};
)~~");
    } else if (declaration.is_variable_declaration()) {
        // Toplevel variable, ignore it.
        if (!generator.maybe_get("class.fqn").has_value())
            return;

        auto& var = static_cast<Cpp::VariableDeclaration&>(declaration);
        if (var.name().starts_with("m_")) {
            // Private instance variable, ignore it.
            return;
        }
        if (var.name().starts_with("on_") && var.type() && var.type()->is_named_type() && static_cast<Cpp::NamedType const*>(var.type())->name()->is_templatized()
            && static_cast<Cpp::NamedType const*>(var.type())->name()->name()->name().ends_with("Function"sv)) {

            // Callback object.
            return;
        }
        // Not something we can handle, ignore it.
        return;
    } else if (declaration.is_function()) {
        auto& fn = static_cast<Cpp::FunctionDeclaration&>(declaration);
        if (fn.is_constructor()) {
            // ctor
            return;
        }
        if (fn.is_destructor()) {
            // dtor, ignore.
            return;
        }
        auto is_in_class = generator.maybe_get("class.fqn").has_value();
        auto is_setter = fn.name().starts_with("set_");
        bool is_getter = false;
        if (fn.parent()) {
            auto found_instance_variable = false;
            auto var_name = String::formatted("m_{}", fn.name());
            for (auto& decl : fn.parent()->declarations()) {
                if (decl.is_variable_declaration() && decl.name() == var_name) {
                    found_instance_variable = true;
                    break;
                }
            }
            if (found_instance_variable) {
                if (!is_setter)
                    is_getter = true;
            } else {
                is_setter = false;
                is_getter = false;
            }
        }

        if (is_in_class) {
            auto fn_generator = generator.fork();
            auto is_static = fn.qualifiers().contains_slow("static"sv);
            fn_generator.set("function.name", fn.name());
            fn_generator.set("function.arity", String::number(fn.parameters().size()));

            if (target == GenerationTarget::InstanceMembers) {
                fn_generator.append(R"~~(
    static JS_DEFINE_NATIVE_FUNCTION(@function.name@)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<@class.object@>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "@class.object@");
            return {};
        }
)~~");
                if (!is_static) {
                    fn_generator.append(R"~~(
        auto* this_ = static_cast<@class.object@*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }
)~~");
                }
                if (fn.parameters().size() == 0) {
                    auto call_expr = is_static
                        ? String::formatted("{}::{}()", generator.get("class.fqn"), fn_generator.get("function.name"))
                        : String::formatted("this_->m_instance->{}()", fn_generator.get("function.name"));
                    auto return_value = convert_to_js_value(call_expr, *fn.return_type());
                    if (return_value.has_value()) {
                        fn_generator.set("return.value", return_value.value());
                        fn_generator.append(R"~~(
        return @return.value@;
)~~");
                    }
                } else {
                    bool should_generate = true;
                    for (auto& param : fn.parameters()) {
                        if (param.is_ellipsis()) {
                            should_generate = false;
                            fn_generator.append(R"~~(
        // FIXME: Call this somehow, needs runtime argument count/conversion.
        // this_->m_instance->@function.name@();
)~~");
                            break;
                        }
                    }
                    if (should_generate) {
                        // FIXME: Need to do overload resolution here
                        //        for now, let's just call the first overload, whatever that may be.
                        auto call_generator = fn_generator.fork();
                        size_t parameter_index = 0;
                        auto should_generate_call = true;
                        for (auto& param : fn.parameters()) {
                            auto type = param.type();
                            call_generator.set("argument.index"sv, String::number(parameter_index));
                            auto maybe_value = convert_from_js_value(String::formatted("vm.argument({})", parameter_index), *type);
                            call_generator.set("type.name"sv, type->to_string());
                            if (!maybe_value.has_value()) {
                                should_generate_call = false;
                                // FIXME: How do we get the string tag of a JS object?
                                call_generator.append(R"~~(
        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "@type.name@");
        return JS::js_null();
)~~");
                                break;
                            }

                            call_generator.set("argument.value"sv, maybe_value.release_value());
                            call_generator.append(R"~~(
        [[maybe_unused]] auto arg_@argument.index@ = @argument.value@;
        if (vm.exception())
            return JS::js_null();
)~~");

                            ++parameter_index;
                        }
                        parameter_index = 0;
                        if (should_generate_call) {
                            if (is_static) {
                                call_generator.append(R"~~(
        @class.fqn@::@function.name@(
)~~");
                            } else {
                                call_generator.append(R"~~(
        this_->m_instance->@function.name@(
)~~");
                            }
                            for ([[maybe_unused]] auto& param : fn.parameters()) {
                                call_generator.set("argument.index"sv, String::number(parameter_index));
                                if (parameter_index + 1 == fn.parameters().size()) {
                                    call_generator.append(R"~~(
            arg_@argument.index@);
)~~");
                                } else {
                                    call_generator.append(R"~~(
            arg_@argument.index@,
)~~");
                                }
                                ++parameter_index;
                            }
                        }
                    }
                }
                fn_generator.append(R"~~(
        return JS::js_null();
    }
)~~");
            } else if (target == GenerationTarget::InitializeInstanceMembers) {
                if (!is_getter && !is_setter) {
                    fn_generator.append(R"~~(
        define_native_function("@function.name@", @function.name@, @function.arity@, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);
)~~");
                } else {
                    auto property = is_setter ? fn.name().substring_view(4) : fn.name();
                    fn_generator.set("function.name.accessor", String::formatted("{} {}", is_setter ? "set" : "get", property));
                    fn_generator.set("function.name.property", property);
                    fn_generator.append(R"~~(
        {
            auto existing_property = storage_get("@function.name.property@").value_or({}).value;
            auto* accessor = existing_property.is_accessor() ? &existing_property.as_accessor() : nullptr;
            auto name = "@function.name.accessor@";
            auto func = JS::NativeFunction::create(global_object, name, @function.name@);
            func->define_direct_property(vm.names.length, JS::Value(1), JS::Attribute::Configurable);
            func->define_direct_property(vm.names.name, JS::js_string(vm.heap(), name), JS::Attribute::Configurable);
            if (!accessor) {
)~~");
                    if (is_getter) {
                        fn_generator.append(R"~~(
                accessor = JS::Accessor::create(vm, func, nullptr);
)~~");
                    } else {
                        fn_generator.append(R"~~(
                accessor = JS::Accessor::create(vm, nullptr, func);
)~~");
                    }
                    fn_generator.append(R"~~(
            } else {
)~~");
                    if (is_getter) {
                        fn_generator.append(R"~~(
                accessor->set_getter(func);
)~~");
                    } else {
                        fn_generator.append(R"~~(
                accessor->set_setter(func);
)~~");
                    }
                    fn_generator.append(R"~~(
            }
            define_direct_property("@function.name.property@", accessor, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);
        }
)~~");
                }
            }
        }
    } else {
        dbgln("Unknown generation target {}", declaration.class_name());
    }
}

int main(int argc, char** argv)
{
    StringView path;

    Core::ArgsParser args_parser;
    args_parser.add_positional_argument(path, "Path to header to generate from", "path");
    args_parser.add_option(Core::ArgsParser::Option {
        .requires_argument = true,
        .help_string = "Add an include path",
        .long_name = "include_path",
        .short_name = 'I',
        .value_name = "path",
        .accept_value = [](char const* s) {
            String path { s };
            if (!Core::File::exists(path)) {
                warnln("Nonexistent include path {}", path);
                return false;
            }
            g_include_paths.append(move(path));
            return true;
        },
    });

    args_parser.parse(argc, argv);

    if (g_include_paths.is_empty()) {
        g_include_paths.append("/usr/src/serenity");
        g_include_paths.append("/usr/src/serenity/Userland/Libraries");
    }

    RootNS namespaces;
    Queue<String> queue;
    HashTable<String> included;
    Vector<Tuple<String, NonnullRefPtr<Cpp::TranslationUnit>>> tus;
    RefPtr<Cpp::TranslationUnit> main_tu;
    queue.enqueue(path);
    do {
        auto path = queue.dequeue();
        dbgln("Try {}", path);
        if (discover(path, namespaces, queue, included, tus) != 0)
            dbgln("FAILED: {}", path);
        else
            main_tu = tus.last().get<1>();
    } while (!queue.is_empty());

    for (auto& entry : namespaces) {
        dbgln("- NS {}:", entry.key ? entry.key->name() : "(global)");
        for (auto& ns_entry : entry.value) {
            ns_entry.visit(
                [](OverloadSet const& os) { dbgln("    - OverloadSet({}) {}", os.size(), os.first()->name()); },
                [](Cpp::VariableDeclaration const* vd) { dbgln("    - var {}", vd->name()); },
                [](RootClass const& rc) {
                    for (auto& entry : rc) {
                        dbgln("    - Class {}", entry.key->name());
                        for (auto& member : entry.value) {
                            member.visit(
                                [](OverloadSet const& os) { dbgln("        - OverloadSet({}) {}", os.size(), os.first()->name()); },
                                [](Cpp::VariableDeclaration const* vd) { dbgln("        - var {}", vd->name()); });
                        }
                    }
                });
        }
    }

    dbgln("Will generate for these guys: {}", main_tu.ptr());

    StringBuilder builder;
    SourceGenerator generator { builder };

    generator.append(String::formatted("#include <{}>\n", path));
    generator.append(R"~~(
#include <LibJS/Runtime/Accessor.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/BigInt.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/VM.h>
)~~");

    for (auto& decl : main_tu->declarations())
        generate(decl, namespaces, generator, GenerationTarget::StaticMembers);

    outln("{}", generator.as_string_view());
}

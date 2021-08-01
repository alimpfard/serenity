#include <LibCore/EventLoop.h>

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

class EventLoopInstanceObject final : public JS::Object {
    JS_OBJECT(EventLoopInstanceObject, JS::Object);

public:
    explicit EventLoopInstanceObject(JS::GlobalObject& global_object, ::Core::EventLoop& instance)
        : Object(*global_object.object_prototype())
        , m_instance(&instance)
    {
    }

    explicit EventLoopInstanceObject(JS::GlobalObject& global_object)
        : Object(*global_object.object_prototype())
        , m_instance(nullptr)
    {
    }

    virtual ~EventLoopInstanceObject() override = default;

    virtual void initialize(JS::GlobalObject& global_object) override
    {
        Base::initialize(global_object);
        [[maybe_unused]] auto& vm = global_object.vm();

        define_native_function("exec", exec, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("pump", pump, 3, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("post_event", post_event, 2, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("main", main, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("current", current, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("was_exit_requested", was_exit_requested, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("register_timer", register_timer, 4, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("unregister_timer", unregister_timer, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("register_notifier", register_notifier, 2, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("unregister_notifier", unregister_notifier, 2, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("quit", quit, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("unquit", unquit, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("take_pending_events_from", take_pending_events_from, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("wake", wake, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("unregister_signal", unregister_signal, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("notify_forked", notify_forked, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("wait_for_event", wait_for_event, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("get_next_timer_expiration", get_next_timer_expiration, 0, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("dispatch_signal", dispatch_signal, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);

        define_native_function("handle_signal", handle_signal, 1, JS::Attribute::Configurable | JS::Attribute::Writable | JS::Attribute::Enumerable);
    }

private:
    static JS_DEFINE_NATIVE_FUNCTION(exec)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        return JS::Value(this_->m_instance->exec());

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(pump)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "WaitMode");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(post_event)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "Object&");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(main)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto& value = ::Core::EventLoop::main();
        return vm.heap().allocate<EventLoopInstanceObject>(global_object, global_object, value);
    }

    static JS_DEFINE_NATIVE_FUNCTION(current)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(was_exit_requested)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        return JS::Value(this_->m_instance->was_exit_requested());

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(register_timer)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "Object&");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(unregister_timer)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        [[maybe_unused]] auto arg_0 = static_cast<i32>((vm.argument(0)).to_double(global_object));
        if (vm.exception())
            return JS::js_null();

        ::Core::EventLoop::unregister_timer(

            arg_0);

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(register_notifier)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "Badge<Notifier>");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(unregister_notifier)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "Badge<Notifier>");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(quit)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        [[maybe_unused]] auto arg_0 = static_cast<i32>((vm.argument(0)).to_double(global_object));
        if (vm.exception())
            return JS::js_null();

        this_->m_instance->quit(

            arg_0);

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(unquit)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        return [&] { this_->m_instance->unquit(); return JS::js_null(); }();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(take_pending_events_from)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "EventLoop&");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(wake)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        return [&] { ::Core::EventLoop::wake(); return JS::js_null(); }();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(unregister_signal)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        [[maybe_unused]] auto arg_0 = static_cast<i32>((vm.argument(0)).to_double(global_object));
        if (vm.exception())
            return JS::js_null();

        ::Core::EventLoop::unregister_signal(

            arg_0);

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(notify_forked)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "ForkEvent");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(wait_for_event)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::Convert, "(FIXMEType)", "WaitMode");
        return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(get_next_timer_expiration)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        auto* this_ = static_cast<EventLoopInstanceObject*>(this_object);
        if (!this_->m_instance) {
            vm.throw_exception<JS::TypeError>(global_object, "Invalid call to instance object on base");
            return {};
        }

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(dispatch_signal)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        [[maybe_unused]] auto arg_0 = static_cast<i32>((vm.argument(0)).to_double(global_object));
        if (vm.exception())
            return JS::js_null();

        return JS::js_null();
    }

    static JS_DEFINE_NATIVE_FUNCTION(handle_signal)
    {
        auto* this_object = vm.this_value(global_object).to_object(global_object);
        if (!this_object || !is<EventLoopInstanceObject>(this_object)) {
            vm.throw_exception<JS::TypeError>(global_object, JS::ErrorType::NotA, "EventLoopInstanceObject");
            return {};
        }

        [[maybe_unused]] auto arg_0 = static_cast<i32>((vm.argument(0)).to_double(global_object));
        if (vm.exception())
            return JS::js_null();

        return JS::js_null();
    }

    ::Core::EventLoop* m_instance;
};

class CoreObject final : public JS::Object {
    JS_OBJECT(CoreObject, JS::Object);

public:
    explicit CoreObject(JS::GlobalObject& global_object)
        : Object(*global_object.object_prototype())
    {
    }

    virtual ~CoreObject() override = default;
    virtual void initialize(JS::GlobalObject& global_object) override
    {
        Base::initialize(global_object);
        [[maybe_unused]] auto& vm = global_object.vm();

        define_direct_property("EventLoop", vm.heap().allocate<EventLoopInstanceObject>(global_object, global_object), JS::Attribute::Enumerable);
    }

private:
};

#include "generated.h"

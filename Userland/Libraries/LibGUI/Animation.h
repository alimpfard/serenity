/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DistinctNumeric.h>
#include <AK/RedBlackTree.h>
#include <AK/Variant.h>
#include <LibCore/Object.h>
#include <LibGfx/Color.h>

namespace GUI {

AK_TYPEDEF_DISTINCT_NUMERIC_GENERAL(u32, false, true, false, false, false, false, AnimationId);

using AnimationInterpolationValue = AK::Variant<i64, u64, double>;

template<typename T>
struct AnimationInterpolationTraits {
    static_assert(AnimationInterpolationValue::can_contain<T>());

    static T from_value(AnimationInterpolationValue value) { return value.get<T>(); }
    static AnimationInterpolationValue to_value(T value) { return value; }
};

template<>
struct AnimationInterpolationTraits<Gfx::Color> {
    static Gfx::Color from_value(AnimationInterpolationValue value) { return Gfx::Color::from_argb(value.get<u64>()); }
    static AnimationInterpolationValue to_value(Gfx::Color value) { return static_cast<u64>(value.value()); }
};

struct AnimationInvocation {
    template<typename T, typename F>
    static NonnullOwnPtr<AnimationInvocation> make(F&& fn)
    {
        return AK::make<AnimationInvocation>([fn = forward<F>(fn)](AnimationInterpolationValue value) mutable {
            fn(AnimationInterpolationTraits<T>::from_value(move(value)));
        });
    }

    Function<void(AnimationInterpolationValue)> callback;
};

class AnimationInterpolator {
public:
    enum class PredefinedType {
        Linear,
        Bezier,
    };

    AnimationInterpolator(PredefinedType type, u64 duration_ms, AnimationInterpolationValue from, AnimationInterpolationValue to)
        : m_type(type)
        , m_duration_ms(duration_ms)
        , m_offset_ms(0)
        , m_from(from)
        , m_to(move(to))
        , m_current(move(from))
    {
    }

    bool is_done();
    void reset();
    AnimationInterpolationValue next();

private:
    PredefinedType m_type;
    u64 m_duration_ms;
    u64 m_offset_ms;
    AnimationInterpolationValue m_from;
    AnimationInterpolationValue m_to;
    AnimationInterpolationValue m_current;
};

struct AnimationProperties {
    u64 delay_ms;
    u64 repeat_count;
    AnimationInterpolator interpolator;
};

struct Animation {
    NonnullOwnPtr<AnimationInvocation> invocation;
    AnimationProperties properties;
};

class AnimationManager : public Core::Object {
    C_OBJECT(AnimationManager)

public:
    static constexpr u64 time_quantum = 10; // ms

    AnimationId add(Animation);
    Vector<AnimationId> add_all(Vector<Animation> animations)
    {
        Vector<AnimationId> ids;
        for (auto& animation : animations)
            ids.append(add(move(animation)));
        return ids;
    }

    void remove(AnimationId);
    void set_playable(AnimationId, bool);

    void timer_event(Core::TimerEvent&) override;

private:
    AnimationManager() = default;

    u32 m_last_given_animation_id { 0 };
    HashMap<AnimationId, Animation> m_animations;

    RedBlackTree<u64, Vector<AnimationId, 4>> m_schedule;
    u64 m_time_offset_ms { 0 };
};

}

/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/Animation.h>
#include <LibGUI/Widget.h>

namespace GUI {

namespace Details {
template<typename F>
struct _FirstParameterTypeOf {
};

template<typename U, typename R, typename Arg, typename... Rest>
struct _FirstParameterTypeOf<R (U::*)(Arg, Rest...)> {
    using Type = Arg;
};

template<typename R, typename Arg, typename... Rest>
struct _FirstParameterTypeOf<R(Arg, Rest...)> {
    using Type = Arg;
};

template<typename T>
using FirstParameterTypeOf = typename _FirstParameterTypeOf<T>::Type;

}

template<typename TWidget>
struct AnimationBuilder {
    AnimationBuilder(TWidget& widget)
        : m_widget(widget)
    {
    }

    template<typename F>
    struct CustomAnimator {
        using DesiredType = Details::FirstParameterTypeOf<decltype(&F::operator())>;

        CustomAnimator(F&& f, AnimationBuilder<TWidget>& builder)
            : m_invocation(AnimationInvocation::make<DesiredType, F>(forward<F>(f)))
            , m_builder(builder)
        {
        }

        auto& end()
        {
            finalize();
            return m_builder;
        }

        auto& duration(u64 ms)
        {
            m_duration_ms = ms;
            return *this;
        }

        auto& bounds(DesiredType from, DesiredType to)
        {
            m_from = AnimationInterpolationTraits<DesiredType>::to_value(from);
            m_to = AnimationInterpolationTraits<DesiredType>::to_value(to);
            return *this;
        }

        auto& repeat(u64 count)
        {
            m_repeat_count = count;
            return *this;
        }

        auto& type(AnimationInterpolator::PredefinedType type)
        {
            m_type = type;
            return *this;
        }

    private:
        void finalize()
        {
            m_builder.add(
                Badge<CustomAnimator<F>> {},
                Animation {
                    .invocation = move(m_invocation),
                    .properties = {
                        .delay_ms = m_delay,
                        .repeat_count = m_repeat_count,
                        .interpolator = AnimationInterpolator(*m_type, m_duration_ms, *m_from, *m_to),
                    },
                });
        }

        u64 m_duration_ms { 1 };
        u64 m_delay { 1 };
        u64 m_repeat_count { 1 };
        NonnullOwnPtr<AnimationInvocation> m_invocation;
        Optional<AnimationInterpolator::PredefinedType> m_type;
        Optional<AnimationInterpolationValue> m_from;
        Optional<AnimationInterpolationValue> m_to;
        AnimationBuilder<TWidget>& m_builder;
    };

    template<typename F>
    auto begin(F&& f) { return CustomAnimator<F>(forward<F>(f), *this); }

    Vector<Animation> end() { return move(m_animations); }

    template<typename U>
    void add(Badge<CustomAnimator<U>>, Animation animation) { m_animations.append(move(animation)); }

private:
    NonnullRefPtr<TWidget> m_widget;
    Vector<Animation> m_animations;
};

}

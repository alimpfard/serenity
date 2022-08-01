/*
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibGUI/Animation.h>

namespace GUI {

AnimationId AnimationManager::add(Animation given_animation)
{
    auto id = AnimationId(m_last_given_animation_id++);
    m_animations.set(id, move(given_animation));

    set_playable(id, true);
    return id;
}

void AnimationManager::remove(AnimationId id)
{
    set_playable(id, false);
    m_animations.remove(id);
}

void AnimationManager::set_playable(AnimationId id, bool playable)
{
    if (!playable) {
        for (auto& it : m_schedule) {
            if (it.remove_first_matching([&](auto& entry) { return entry == id; }))
                break;
        }
        return;
    }

    auto& animation = *m_animations.get(id);
    auto* bucket = m_schedule.find(animation.properties.delay_ms);
    if (!bucket) {
        m_schedule.insert(animation.properties.delay_ms, {});
        bucket = m_schedule.find(animation.properties.delay_ms);
    }
    VERIFY(bucket != nullptr);

    bucket->append({ id });
}

void AnimationManager::timer_event(Core::TimerEvent&)
{
    auto it = m_schedule.find_largest_not_above_iterator(m_time_offset_ms);

    if (it.is_end()) {
        m_time_offset_ms += time_quantum;
        return;
    }

    Core::deferred_invoke([this, schedule = *it] {
        for (auto& id : schedule) {
            auto& animation = *m_animations.get(id);
            if (animation.properties.interpolator.is_done()) {
                if (animation.properties.repeat_count-- == 0) {
                    remove(id);
                    return;
                }
                animation.properties.interpolator.reset();
            }

            auto next_value = animation.properties.interpolator.next();
            animation.invocation->callback(next_value);
        }
    });

    m_schedule.remove(it.key());
    if (m_schedule.is_empty()) {
        m_time_offset_ms = 0;
        for (auto& entry : m_animations)
            set_playable(entry.key, true);
    } else {
        m_time_offset_ms += time_quantum;
    }
}

void AnimationInterpolator::reset()
{
    m_current = m_from;
    m_offset_ms = 0;
}

bool AnimationInterpolator::is_done()
{
    return m_offset_ms >= m_duration_ms;
}

static double bezier(double t, double p1 = 0.3, double p2 = 0.7)
{
    return 3 * (1 - t) * (1 - t) * t * p1 + 3 * (1 - t) * t * t * p2 + t * t * t;
}

AnimationInterpolationValue AnimationInterpolator::next()
{
    auto current = m_current;
    m_current.visit([&]<typename T>(T& value) {
        switch (m_type) {
        case PredefinedType::Linear:
            value += (m_to.get<T>() - m_from.get<T>()) / m_duration_ms;
            break;
        case PredefinedType::Bezier:
            value = (m_to.get<T>() - m_from.get<T>()) * bezier((1.0 * m_offset_ms) / (1.0 * m_duration_ms)) + m_from.get<T>();
            break;
        }
    });

    m_offset_ms += AnimationManager::time_quantum;
    return current;
}

}

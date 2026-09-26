#include "mh/Scheduler.h"

namespace mh {

bool Scheduler::addTask(uint8_t id, uint32_t period_ms, uint32_t now_ms,
                        uint32_t first_delay_ms) {
    if (count_ >= kMaxTasks || period_ms == 0) {
        return false;
    }
    PeriodicTask& t = tasks_[count_++];
    t.id = id;
    t.period_ms = period_ms;
    t.next_due_ms = now_ms + first_delay_ms;
    t.enabled = true;
    return true;
}

size_t Scheduler::tick(uint32_t now_ms, uint8_t* out_ids, size_t max_out) {
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
        PeriodicTask& t = tasks_[i];
        if (!t.enabled) continue;
        if (!isDue(now_ms, t.next_due_ms)) continue;

        if (n < max_out) {
            out_ids[n++] = t.id;
        }

        // Schemalagg om. Om vi har halkat efter mer an en hel period hoppar vi
        // fram till nasta framtida tillfalle istallet for att kora ikapp. Se
        // kommentaren i headern.
        t.next_due_ms += t.period_ms;
        if (isDue(now_ms, t.next_due_ms)) {
            t.next_due_ms = now_ms + t.period_ms;
        }
    }
    return n;
}

void Scheduler::setEnabled(uint8_t id, bool enabled) {
    for (size_t i = 0; i < count_; ++i) {
        if (tasks_[i].id == id) {
            tasks_[i].enabled = enabled;
            return;
        }
    }
}

void Scheduler::setPeriod(uint8_t id, uint32_t period_ms, uint32_t now_ms) {
    if (period_ms == 0) return;
    for (size_t i = 0; i < count_; ++i) {
        if (tasks_[i].id == id) {
            tasks_[i].period_ms = period_ms;
            tasks_[i].next_due_ms = now_ms + period_ms;
            return;
        }
    }
}

uint32_t Scheduler::msUntilNext(uint32_t now_ms) const {
    uint32_t best = 0xFFFFFFFFu;
    for (size_t i = 0; i < count_; ++i) {
        const PeriodicTask& t = tasks_[i];
        if (!t.enabled) continue;
        const uint32_t remaining = t.next_due_ms - now_ms; // osignerat, talar varv
        if (static_cast<int32_t>(remaining) <= 0) {
            return 0;
        }
        if (remaining < best) {
            best = remaining;
        }
    }
    return best == 0xFFFFFFFFu ? 0 : best;
}

} // namespace mh

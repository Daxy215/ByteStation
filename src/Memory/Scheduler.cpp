#include "Scheduler.h"

#include <algorithm>

void Scheduler::scheduleEvent(EventFunc eventFunc, uint64_t cyclesFromNow) {
    uint64_t cycle = _currentCycle + cyclesFromNow;

    _events.push_back({cycle, std::move(eventFunc)});

    _nextEventCycle = std::min(_nextEventCycle, cycle);
}

void Scheduler::catchUp() {
    while (!_events.empty()) {
        auto it = std::min_element(_events.begin(), _events.end(),
            [](const Event& a, const Event& b) { return a.cycle < b.cycle; });

        if (it->cycle > _currentCycle)
            break;

        EventFunc eventFunc = std::move(it->eventFunc);
        _events.erase(it);

        eventFunc();
    }

    recomputeNextEvent();
}

void Scheduler::forceRecompute() {
    recomputeNextEvent();
}

void Scheduler::reset() {
    _events.clear();
    _currentCycle = 0;
    _nextEventCycle = 0;
}

void Scheduler::recomputeNextEvent() {
    uint64_t next = _currentCycle + MAX_SLICE;

    for (const auto& event : _events)
        next = std::min(next, event.cycle);

    _nextEventCycle = std::max(next, _currentCycle + 1);
}

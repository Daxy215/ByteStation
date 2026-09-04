#include "Scheduler.h"

void Scheduler::scheduleEvent(EventFunc eventFunc, uint64_t cyclesFromNow) {
    uint64_t cycle = _currentCycle + cyclesFromNow;

    _events[_eventCount++] = {cycle, eventFunc};

    if (cycle < _nextEventCycle)
        _nextEventCycle = cycle;
}

void Scheduler::catchUp() {
    while (_eventCount > 0) {
        size_t minIndex = 0;

        for (size_t i = 1; i < _eventCount; i++) {
            if (_events[i].cycle < _events[minIndex].cycle)
                minIndex = i;
        }

        if (_events[minIndex].cycle > _currentCycle)
            break;

        EventFunc eventFunc = _events[minIndex].eventFunc;

        for (size_t i = minIndex; i + 1 < _eventCount; i++)
            _events[i] = _events[i + 1];

        _eventCount--;

        eventFunc(_interconnect);
    }

    recomputeNextEvent();
}

void Scheduler::forceRecompute() {
    recomputeNextEvent();
}

void Scheduler::reset() {
    _eventCount = 0;
    _currentCycle = 0;
    _nextEventCycle = 0;
}

void Scheduler::recomputeNextEvent() {
    uint64_t next = _currentCycle + MAX_SLICE;

    for (size_t i = 0; i < _eventCount; i++) {
        if (_events[i].cycle < next)
            next = _events[i].cycle;
    }

    if (next <= _currentCycle)
        next = _currentCycle + 1;

    _nextEventCycle = next;
}

#pragma once

#include <cstddef>
#include <cstdint>

class Interconnect;

class Scheduler {
public:
    using EventFunc = void (*)(Interconnect*);

    explicit Scheduler(Interconnect* interconnect) : _interconnect(interconnect) {}

    void rebind(Interconnect* interconnect) { _interconnect = interconnect; }

    void addCycles(uint32_t cycles) {
        _currentCycle += cycles;

        if (_currentCycle >= _nextEventCycle)
            catchUp();
    }

    void scheduleEvent(EventFunc eventFunc, uint64_t cyclesFromNow);

    void catchUp();
    void forceRecompute();
    void reset();

    uint64_t currentCycle() const { return _currentCycle; }

private:
    struct Event {
        uint64_t cycle;
        EventFunc eventFunc;
    };

    void recomputeNextEvent();

    Interconnect* _interconnect;

    static constexpr size_t MAX_EVENTS = 4;
    Event _events[MAX_EVENTS];
    size_t _eventCount = 0;

    uint64_t _currentCycle = 0;
    uint64_t _nextEventCycle = 0;

    static constexpr uint64_t MAX_SLICE = 512;
};

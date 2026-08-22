#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class Interconnect;

class Scheduler {
public:
    using EventFunc = std::function<void()>;

    explicit Scheduler(Interconnect* interconnect) : _interconnect(interconnect) {}

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

    std::vector<Event> _events;

    uint64_t _currentCycle = 0;
    uint64_t _nextEventCycle = 0;

    static constexpr uint64_t MAX_SLICE = 512;
};

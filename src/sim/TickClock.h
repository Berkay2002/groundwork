#pragma once
#include <algorithm>

// Fixed-timestep accumulator for the simulation loop. It is deliberately
// GL-free and clock-source-free: callers feed measured frame seconds, tests
// feed synthetic sequences.
class TickClock {
public:
    static constexpr double TICK_DT = 0.05; // 20 TPS
    static constexpr int MAX_TICKS_PER_FRAME = 5;

    int advance(double dt, bool paused) {
        consumedSeconds_ = 0.0;
        if (paused) return 0;

        accumulator_ += std::max(0.0, dt);
        int ticks = 0;
        while (accumulator_ >= TICK_DT) {
            if (ticks == MAX_TICKS_PER_FRAME) {
                accumulator_ = 0.0;
                break;
            }
            ++ticks;
            accumulator_ -= TICK_DT;
        }
        consumedSeconds_ = double(ticks) * TICK_DT;
        return ticks;
    }

    float alpha() const {
        return float(accumulator_ / TICK_DT);
    }

    double consumedSeconds() const {
        return consumedSeconds_;
    }

private:
    double accumulator_ = 0.0;
    double consumedSeconds_ = 0.0;
};

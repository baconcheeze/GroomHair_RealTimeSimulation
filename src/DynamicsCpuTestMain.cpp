#include "Dynamics.h"
#include <iostream>
int main()
{
    try
    {
        FixedStepClock clock;
        double h = 1. / 120;
        unsigned steps = 0;
        for (unsigned frame = 0; frame < 60; frame++)
            steps += clock.advance(1. / 60, h, true);
        if (steps != 120)
            throw std::runtime_error("Fixed clock step count");
        clock.advance(1, h, false);
        if (clock.remainder != 0)
            throw std::runtime_error("Pause catchup");
        if (clock.advance(1, h, true) != 8 || std::abs(clock.discarded - (1 - 8 * h)) > 1e-12)
            throw std::runtime_error("Stall cap accounting");
        FixedStepClock a, b;
        unsigned na = 0, nb = 0;
        for (unsigned i = 0; i < 240; i++)
            na += a.advance(1. / 240, h, true);
        for (unsigned i = 0; i < 30; i++)
            nb += b.advance(1. / 30, h, true);
        if (na != nb || na != 120)
            throw std::runtime_error("Render-frame grouping");
        std::cout << "PASS CPU fixed clock: pause, grouping, stall cap and discarded time\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

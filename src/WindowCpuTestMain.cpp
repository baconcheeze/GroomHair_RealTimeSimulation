#include "WindowMath.h"
#include <iostream>
int main()
{
    try
    {
        std::cout << windowmath::validatePositionMath() << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#include <iostream>
#include "engine/engine.hpp"

int main()
{
    std::cout << "<======== Engine ========>" << std::endl;

    engine::Engine engine;
    engine.run();
}

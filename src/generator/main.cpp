#include <iostream>
#include "generator/generator.hpp"

int main()
{
    std::cout << "======== Generator ========" << std::endl;
    generator::Generator gen;
    gen.connect("127.0.0.1", 8888);
    gen.run(3000, 0, 1);
}
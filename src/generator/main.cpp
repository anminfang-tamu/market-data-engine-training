#include <iostream>
#include "generator/generator.hpp"

int main()
{
    std::cout << "======== Generator ========" << std::endl;
    generator::Generator gen;
    gen.connect("127.0.0.1", 8888);

    // 10-minute soak
    const int rate = 50'000;
    const int duration_s = 600; // 10 minutes
    const int count = rate * duration_s;
    const int seed = 42;

    gen.run(count, rate, seed);

    // burst + normal
    const int burst_count = 100'000;
    const int burst_rate = 0;
    const int burst_seed = 43;

    const int normal_rate = 50'000;
    const int normal_duration = 600; // seconds
    const int normal_count = normal_rate * normal_duration;
    const int normal_seed = 44;

    gen.run(burst_count, burst_rate, burst_seed);
    gen.run(normal_count, normal_rate, normal_seed);

    return 0;
}
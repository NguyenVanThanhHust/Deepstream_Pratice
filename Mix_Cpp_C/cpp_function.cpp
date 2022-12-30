
#include <iostream>
#include <vector>
#include "cpp_header.h"

using std::cout;

float cal_sum(float a, float b)
{
    std::vector<float> array;
    for (int i = 0; i < 100; i++)
    {
        array.push_back(b);
    }
    
    float c = a +b;
    for (int i = 0; i < 2; i++)
    {
        float tmp = array.front();
        c += tmp;
    }
    return c;
}
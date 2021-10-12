#include <vector>
#include <cassert>
#include <stdexcept>
#include <iostream>

int main()
{
    std::vector<int> v1 = { 0, 1, 2 };
    assert(v1.size() == 3);
    assert(v1.at(0) == 0);
    assert(v1.at(1) == 1);
    assert(v1.at(2) == 2);

    try
    {
        (void) v1.at(3);
        assert(false);
    }
    catch (const std::out_of_range &)
    {
        std::cout << "caught an exception!" << std::endl;
    }
}

#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>

int main(int argc, char *argv[]) {
    std::ifstream is("container_list");
    std::istream_iterator<int> start(is), end;
    std::vector<int> numbers(start, end);

    std::copy(numbers.begin(), numbers.end(), std::ostream_iterator<int>(std::cout, "\n"));
}
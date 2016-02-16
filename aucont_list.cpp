#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <algorithm>
#include <linux/limits.h>
#include <unistd.h>

std::string getexepath()
{
    char result[ PATH_MAX ];
    ssize_t count = readlink( "/proc/self/exe", result, PATH_MAX );
    return std::string( result, (count > 0) ? count : 0);
}

int main(int argc, char *argv[]) {
    std::string cur_dir = getexepath();
    chdir (cur_dir.c_str());

    std::ifstream is("container_list");
    std::istream_iterator<int> start(is), end;
    std::vector<int> numbers(start, end);

    std::copy(numbers.begin(), numbers.end(), std::ostream_iterator<int>(std::cout, "\n"));
}
#include <iostream>
#include <vector>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <unistd.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)

int signum = 15;

int main(int argc, char *argv[]) {
    if (argc > 2) {
        signum = atoi(argv[2]);
    }

    int pid = atoi(argv[1]);

    int err = system(("sudo kill -" + std::to_string(signum) + " " + std::to_string(pid)).c_str());
    if (err < 0) {
        errExit("kill");
    }

    sleep(1);

    std::ifstream is("container_list");
    std::istream_iterator<int> start(is), end;
    std::vector<int> numbers(start, end);
    numbers.erase(std::remove(numbers.begin(), numbers.end(), pid), numbers.end());

    std::ofstream output_file("container_list");
    std::ostream_iterator<int> output_iterator(output_file, "\n");
    std::copy(numbers.begin(), numbers.end(), output_iterator);

    system(("rmdir /tmp/cgroup/cpu/" + std::to_string(pid)).c_str());
    return 0;
}
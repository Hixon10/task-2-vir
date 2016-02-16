#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <sys/wait.h>
#include <grp.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)

using namespace std;

int fd[2];
int pid;

void set_ns();

int main(int argc, char *argv[]) {
    int err;

    pid = atoi(argv[1]);
    vector<char *> args;

    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    pipe(fd);

    err = system(("echo " + to_string(getpid()) + " >> /tmp/cgroup/cpu/" + to_string(pid) + "/tasks").c_str());
    if (err < 0) {
        errExit("echo tasks");
    }

    set_ns();

    int pd = fork();
    if (pd == 0) {
        setgroups(0, nullptr);
        setgid(0);
        setuid(0);

        err = execvp(args[0], &args[0]);
        if (err < 0) {
            errExit("execvp cmd");
        }
    } else {
        close(fd[1]);
        waitpid(pd, NULL, 0);
    }

    return 0;
}

void set_ns() {
    /*
     *  Each process has a /proc/[pid]/ns/ subdirectory containing one entry
       for each namespace that supports being manipulated by setns
     */

    vector<string> names = {"user", "ipc", "uts", "net", "pid", "mnt"};

    for (string ns : names) {
        int fd_ns = open(("/proc/" + to_string(pid) + "/ns/" + ns).c_str(), O_RDONLY, O_CLOEXEC);
        if (fd_ns == -1) {
            errExit("fd_ns");
        }

        setns(fd_ns, 0);
        int err = close(fd_ns);

        if (err < 0) {
            errExit("close(fd_ns)");
        }
    }
}
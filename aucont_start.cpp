#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <grp.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <thread>
#include <string.h>
#include <dirent.h>

using namespace std;

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                               } while (0)

// NOTE: only works for IPv4.  Check out inet_pton/inet_ntop for IPv6 support.
char* increment_address(const char* address_string)
{
    // convert the input IP address to an integer
    in_addr_t address = inet_addr(address_string);

    // add one to the value (making sure to get the correct byte orders)
    address = ntohl(address);
    address += 1;
    address = htonl(address);

    // pack the address into the struct inet_ntoa expects
    struct in_addr address_struct;
    address_struct.s_addr = address;

    // convert back to a string
    return inet_ntoa(address_struct);
}


bool is_daemonize = false;
int cpu_perc = 100;
string image_path = "";
string ip_container = "";
string cmd = "";
vector<char *> cmd_args;
string netid = "";
int pid;

int fd[2];

void net_main();

void net_container();


void map_uid_and_gid(int uid, int gid);
void set_cpu_limit();

void add_container_to_list();
int container_main(void *d);

void mount_image();

void mount_file_system();

void mount_cgroups();

#define STACK_SIZE (1024 * 1024)    /* Stack size for cloned child */
char *stack;
char *stackTop;

void parse_args(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            is_daemonize = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            cpu_perc = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--net") == 0) {
            ip_container = argv[++i];
        } else if (image_path.empty()) {
            image_path = argv[i];
        } else if (cmd.empty()) {
            cmd = argv[i];
            cmd_args.push_back(argv[i]);
        } else {
            cmd_args.push_back(argv[i]);
        }
    }
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    int uid = getuid();
    int gid = getgid();

    netid = "Net" + to_string(getpid());

    pipe(fd);

    stack = new char[STACK_SIZE];
    if (stack == NULL) {
        errExit("malloc");
    }
    stackTop = stack + STACK_SIZE;  /* Assume stack grows downward */

    pid = clone(container_main, stackTop,
                CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD, nullptr);

    if (pid == -1) {
        errExit("clone");
    }

    mount_cgroups();

    map_uid_and_gid(uid, gid);

    set_cpu_limit();

    net_main();

    cout << pid << endl;

    close(fd[1]);

    if (is_daemonize) {
        add_container_to_list();
    } else {
        waitpid(pid, NULL, 0);
    }

    return 0;
}

void mount_cgroups() {
    int err;
    string base_path = "/tmp/cgroup";
    vector<string> names = {"cpu", "memory", "blkio", "cpuacct", "cpuset"};

    mkdir(base_path.c_str(), 0755);

    for (string name : names) {
        struct stat sb;
        if (stat((base_path + "/" + name).c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
        {
        } else {
            system(("sudo mkdir -p /tmp/cgroup/" + name).c_str());
            system(("sudo mount -t cgroup " + name + " -o " + name + " /tmp/cgroup/" + name).c_str());
        }
    }

    system(("sudo chown 1000:1000 -R " + base_path + "/cpu").c_str());
    system(("sudo chmod 755 -R " + base_path + "/cpu").c_str());
}

void add_container_to_list() {
    ofstream list("container_list", ofstream::app | ofstream::out);
    list << pid << "\n";
    list.close();
}

int container_main(void *d) {
    int err;

    char c;
    close(fd[1]);
    read(fd[0], &c, 1);

    if (mount("none", (image_path + "/proc").c_str(), "proc", MS_NOEXEC|MS_NOSUID|MS_NODEV, NULL) == -1) {
        errExit("proc");
    }

    err = chdir(image_path.c_str());
    if (err < 0) {
        errExit("chdir");
    }

    mount_file_system();

    mount_image();

    std::string container_name = "container";
    err = sethostname(container_name.c_str(), container_name.length());
    if (err != 0) {
        errExit("sethostname");
    }

    setgroups(0, nullptr);
    umask(0);

    int sid = setsid();
    if (sid < 0)
    {
        exit(EXIT_FAILURE);
    }

    net_container();

    if (is_daemonize) {
        if ((chdir("/")) < 0) {
            errExit("chdir");
        }

        freopen("/dev/null", "r", stdin);
        freopen("/tmp/err", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }

    if (execv(cmd.c_str(), &cmd_args[0]) == -1) {
        errExit("execv");
    }

    return 0;
}

void mount_file_system() {
    int err;

    err = mkdir("sys", 0755);
    if (err < 0 && errno != EEXIST) {
        errExit("mkdir sys");
    }

    mount("sys", (image_path + "/sys").c_str(), "sysfs", 0, nullptr);

    err = mkdir("dev/shm", 0755);
    if (err < 0 && errno != EEXIST) {
        errExit("mkdir dev/shm");
    }

    struct stat sb;
    if (stat("/dev/mqueue", &sb) == 0 && S_ISDIR(sb.st_mode)) {
        err = mkdir("dev/mqueue", 0755);
        if (err < 0 && errno != EEXIST) {
            errExit("mkdir dev/mqueue");
        }

        err = mount("/dev/mqueue", "dev/mqueue", NULL, MS_BIND, nullptr);
        if (err < 0) {
            errExit("mount /dev/mqueue");
        }
    }
}

void mount_image() {
    int err;

    err = mkdir("./old", 0777);
    if (err < 0 && errno != EEXIST) {
        errExit("mkdir ./old");
    }

    err = mount(image_path.c_str(), image_path.c_str(), "bind", MS_BIND | MS_REC, nullptr);
    if (err < 0) {
        errExit("mount image_path");
    }

    err = syscall(SYS_pivot_root, image_path.c_str(), (image_path + "/old").c_str());
    if (err < 0) {
        errExit("change root dir");
    }

    err = chdir("/bin");
    if (err < 0) {
        errExit("chdir /bin");
    }

    err = umount2("/old", MNT_DETACH);
    if (err < 0) {
        errExit("umount2");
    }
}

void set_cpu_limit() {
    int err;

    string cgroups_path = "/tmp/cgroup/cpu/" + to_string(pid);

    err = system(("mkdir -p " + cgroups_path).c_str());
    if (err < 0) {
        errExit("mkdir");
    }

    err = system(("chown 1000:1000 -R " + cgroups_path).c_str());
    if (err < 0) {
        errExit("chown");
    }

    //100000 (100msec) is default
    err = system(("echo 1000000 >> " + cgroups_path + "/cpu.cfs_period_us").c_str());
    if (err < 0) {
        errExit("echo 1000000 period");
    }

    int new_cpu_quota = (1000000 / 100) * cpu_perc * thread::hardware_concurrency();
    err = system(("echo " + to_string(new_cpu_quota) + " >> " + cgroups_path + "/cpu.cfs_quota_us").c_str());
    if (err < 0) {
        errExit("set new cpu quota");
    }

    err = system(("echo " + to_string(pid) + " >> " + cgroups_path + "/tasks").c_str());
    if (err < 0) {
        errExit("echo tasks");
    }
}

void map_uid_and_gid(int uid, int gid) {
    /*
    When a user namespace is created, it starts out without a mapping of
    user IDs (group IDs) to the parent user namespace.
    The  /proc/[pid]/uid_map and /proc/[pid]/gid_map files
    expose the mappings for user and group IDs inside the user
    namespace for the process pid.
     */

    string filename = "/proc/" + to_string(pid) + "/uid_map";
    ofstream out_uid(filename);

    out_uid << "0 " + to_string(uid) + " 1\n";
    out_uid.close();

    filename = "/proc/" + to_string(pid) + "/gid_map";
    ofstream out_gid(filename);

    out_gid << "0 " + to_string(gid) + " 1\n";
    out_gid.close();
}


void net_main() {
    if (ip_container.empty()) {
        return;
    }

    int err;

    err = system(("sudo ip link add name u-" + netid + "-0 type veth peer name u-" + netid + "-1").c_str());
    if (err < 0) {
        errExit("ip link add");
    }

    err = system(("sudo ip link set u-" + netid + "-1 netns " + to_string(pid)).c_str());
    if (err < 0) {
        errExit("ip link set");
    }

    err = system(("sudo ip link set u-" + netid + "-0 up").c_str());
    if (err < 0) {
        errExit("ip link set");
    }

    err = system(("sudo ip addr add " + string(increment_address(ip_container.c_str())) + "/24 dev u-" + netid + "-0").c_str());
    if (err < 0) {
        errExit("ip addr add");
    }
}

void net_container() {
    if (ip_container.empty()) {
        return;
    }

    int err;

    err = system("ip link set lo up");
    if (err < 0) {
        errExit("set lo up");
    }

    err = system(("ip link set u-" + netid + "-1 up").c_str());
    if (err < 0) {
        errExit("set veth1 up");
    }

    err = system(("ip addr add " + ip_container + "/24 dev u-" + netid + "-1").c_str());
    if (err < 0) {
        errExit("ip addr add");
    }
}
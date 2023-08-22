#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/mount.h>
#include <dirent.h>

static char child_stack[1048576];

struct clone_args{
	char* rootfs;
	char* hostname;
};

static int child_fn(void* arg) {
	struct clone_args *args = (struct clone_args *)arg;
 	chroot(args->rootfs);
 	chdir("/");
 	mount("proc", "/proc", "proc", 0, NULL);
 	sethostname(args->hostname, strlen(args->hostname));
 	char *exec_args[] = {"/bin/bash", NULL};
 	execvp(exec_args[0], exec_args);
  	return 0;
}

void set_cgroup(long pid, char* cgroup){ // for cgroups-v2
	char path[256] = {'\0'};
	strcat(path, "/sys/fs/cgroup/");
	strcat(path, cgroup);
	char command[256];
	DIR* dir = opendir(path);
	if(!dir && ENOENT == errno) {
		sprintf(command, "mkdir %s", path);
		system(command);
		sprintf(command, "echo \"100000000\" > %s/memory.max", path); // 100 MB memory limit
		system(command);
		sprintf(command, "echo \"0\" > %s/memory.swap.max", path); // turn off swap
		system(command);
	}
	sprintf(command, "echo \"%ld\" >> %s/cgroup.procs", pid, path); // add child to cgroup
	system(command);
}

int main(int argc, char* argv[]) {
	if(argc != 5){
		printf("Error: need 4 args -- rootfs, hostname, container ID, cgroup\n");
		return 1;
	}
	struct clone_args args;
	args.rootfs = argv[1];
	args.hostname = argv[2];

	pid_t child_pid = clone(child_fn, child_stack+1048576, 
							CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD, 
							(void *)(&args)); // clone child with new namespaces
	
	printf("%s\n",strerror(errno));
	printf("child pid = %ld\n", (long)child_pid);
	
	char setup_veth[256];
	char lo_up[256];
	char veth_up[256];
	char ip_host[256];
	char ip_cont[256];
	
	sprintf(setup_veth, "ip link add name veth%s type veth peer name veth0 netns %ld", 
			argv[3], (long)child_pid); // create veth pair
	sprintf(lo_up, "nsenter -t %ld -n ip link set lo up", (long)child_pid);
	sprintf(veth_up, "nsenter -t %ld -n ip link set veth0 up", (long)child_pid);
	sprintf(ip_host, "ip addr add 10.1.%s.1/24 dev veth%s", argv[3], argv[3]);
	sprintf(ip_cont, "nsenter -t %ld -n ip addr add 10.1.%s.2/24 dev veth0", 
			(long)child_pid, argv[3]);
	
	system(setup_veth);
	system(lo_up);
	system(veth_up);
	system(ip_host);
	system(ip_cont);

	set_cgroup(child_pid, argv[4]);

	waitpid(child_pid, NULL, 0);

	return 0;
}


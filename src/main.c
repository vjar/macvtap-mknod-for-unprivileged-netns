#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <net/if.h>
#include <ctype.h>

typedef struct format_ns_path {
	char path[256];
	int32_t target_pid;
} format_ns_path_t;

typedef struct setnsinfo {
	int32_t userns_fd;
	int32_t netns_fd;
} setnsinfo_t;

dev_t* allocate_dev_shm() {
	dev_t *shm = mmap(
		NULL,
		sizeof(*shm),
		PROT_READ | PROT_WRITE,
		MAP_ANON | MAP_SHARED,
		-1,
		0
	);
	return shm;
}

int32_t wait_for_child(int32_t *pRv) {
	int32_t wstatus;
	waitpid(
		/* pid: wait on any pid */ -1,
		/* wstatus */ &wstatus,
		/* options */ 0
	);
	if (pRv != NULL) {
		*pRv = WEXITSTATUS(wstatus);
	}
	if (wstatus) {
		fprintf(stderr, "wait status unsuccessful, exit code %d\n", WEXITSTATUS(wstatus));
		return 1;
	}
	return 0;
}

int32_t discover_target_pid(int32_t *pPid, char *argv[], int32_t *infd, int32_t *outfd) {
	if (pipe(infd) == -1) {
		perror("pipe");
		return 1;
	}

	if (pipe(outfd) == -1) {
		perror("pipe");
		return 1;
	}

	// infd[0] = readable fd of child's stdin
	// infd[1] = writable fd of child's stdin
	// outfd[0] = readable fd of child's stdout
	// outfd[1] = writable fd of child's stdout
	if (fork() == 0) {
		// closes for the unused duplicates created by fork
		if (close(infd[1])) { perror("close"); exit(1); }
		if (close(outfd[0])) { perror("close"); exit(1); }

		if (dup2(infd[0], STDIN_FILENO) == -1) { perror("dup2"); exit(1); }
		if (dup2(outfd[1], STDOUT_FILENO) == -1) { perror("dup2"); exit(1); }
		// closes for the ones left open after duplicating over the
		// standard streams
		if (close(infd[0])) { perror("close"); exit(1); }
		if (close(outfd[1])) { perror("close"); exit(1); }

		if (execve(PID_DISCOVERY_EXEC,
			argv,
			&(char*){ NULL }
		)) {
			perror("execve");
			exit(1);
		}
		exit(0);
	} else {
		if (close(infd[0])) { perror("close"); exit(1); }
		if (close(outfd[1])) { perror("close"); exit(1); }

		char buf[16];
		int64_t nbytes = read(outfd[0], buf, sizeof(buf));
		if (nbytes == -1) {
			perror("read");
			return 1;
		}

		buf[nbytes] = '\0';

		int32_t pid = atoi(buf);
		if (pid <= 0) {
			fprintf(stderr, "child wrote invalid pid \"%s\"\n", buf);
			return 1;
		}
		*pPid = pid;
		return 0;
	}
}


int32_t format_ns_path(format_ns_path_t *fmt, char* ns) {
	int32_t nbytes = snprintf(fmt->path, sizeof(fmt->path), "/proc/%d/ns/%s", fmt->target_pid, ns);
	assert(nbytes != -1 && (uint64_t)nbytes < sizeof(fmt->path));
	return 0;
}

int32_t open_ns_fds(setnsinfo_t *info, int32_t target_pid) {
	format_ns_path_t *fmt = &(format_ns_path_t){
		.target_pid = target_pid,
	};
	if (format_ns_path(fmt, "user")) { return 1; }
	info->userns_fd = open(fmt->path, O_RDONLY | O_CLOEXEC);
	if (format_ns_path(fmt, "net")) { return 1; }
	info->netns_fd = open(fmt->path, O_RDONLY | O_CLOEXEC);
	return 0;
}

int32_t do_setns(setnsinfo_t *info) {
	if (setns(info->userns_fd, CLONE_NEWUSER)) {
		perror("setns");
		return 1;
	}

	if (setns(info->netns_fd, CLONE_NEWNET)) {
		perror("setns");
		return 1;
	}

	close(info->userns_fd);
	close(info->netns_fd);

	if (unshare(CLONE_NEWNS)) {
		perror("unshare");
		return 1;
	}
	return 0;
}

int32_t mount_correct_sysfs(setnsinfo_t *setns_i) {
	if (do_setns(setns_i)) { return 1; }

	if (mount("none", "/sys", "sysfs", 0, NULL)) {
		perror("mount");
		return 1;
	}
	return 0;
}

int32_t macvtap_uevent_fd(char *ifname, int32_t *pFd) {
	char path[256];
	int32_t nbytes = snprintf(path, sizeof(path), "/sys/devices/virtual/net/%s/macvtap", ifname);
	assert(nbytes != -1 && (uint64_t)nbytes < sizeof(path));

	DIR *dir = opendir(path);
	if (dir == NULL) {
		perror("opendir");
		return 1;
	}

	// only expecting this dir to have one dir inside, reading just the
	// first one here
	readdir(dir); readdir(dir); // skip . and ..
	struct dirent *tapdir = readdir(dir);

	nbytes = snprintf(path, sizeof(path), "/sys/devices/virtual/net/%s/macvtap/%s/uevent", ifname, tapdir->d_name);
	assert(nbytes != -1 && (uint64_t)nbytes < sizeof(path));
	closedir(dir);

	*pFd = open(path, O_RDONLY | O_CLOEXEC);
	if (*pFd == -1) {
		perror("open");
		return 1;
	}
	return 0;
}

int32_t parse_uevent_file(dev_t *pDev, int32_t fd) {
	char buf[256];
	int64_t nbytes = read(fd, buf, sizeof(buf));
	if (nbytes < 0) {
		perror("read");
		return 1;
	}
	buf[nbytes] = '\0';
	close(fd);

	char *newline;
	newline = strtok(buf, "\n");
	char *major_str, *minor_str;
	for (;;) {
		int64_t keylen = strstr(newline, "=") - newline;
		newline[keylen] = '\0';
		char *key = newline;
		char *value_str = &newline[keylen+1];

		if (strcmp(key, "MAJOR") == 0) {
			major_str = value_str;
		} else if (strcmp(key, "MINOR") == 0) {
			minor_str = value_str;
		}

		newline = strtok(NULL, "\n");
		if (newline == NULL) { break; }
	}

	if (major_str && minor_str) {
		int major = atoi(major_str);
		int minor = atoi(minor_str);
		assert(major > 0 && minor > 0);
		*pDev = makedev((uint32_t)major, (uint32_t)minor);
	}
	return 0;
}

int32_t read_majmin_in_netns(char *ifname, dev_t *pDev, setnsinfo_t *setns_i) {
	assert(pDev != NULL);
	if (fork() == 0) {
		if (mount_correct_sysfs(setns_i)) { exit(1); }

		int32_t uevent_fd;
		if (macvtap_uevent_fd(ifname, &uevent_fd)) { exit(1); }
		if (parse_uevent_file(pDev, uevent_fd)) { exit(1); }
		exit(0);
	} else {
		int32_t rv;
		if (wait_for_child(&rv)) {
			fprintf(stderr, "child exited with code %d\n", rv);
			return 1;
		}

		return 0;
	}
}

int32_t open_target_pid_ns_fds(setnsinfo_t *setns_i, char *argv[]) {
	int32_t target_pid;
	int32_t infd[2];
	int32_t outfd[2];
	// the child is left running until after the namespace fds in
	// `/proc/PID/ns/` are opened.
	if (discover_target_pid(&target_pid, argv, infd, outfd)) { return 1; }

	if (open_ns_fds(setns_i, target_pid)) { return 1; }
	// close the parent's side of the child's standard streams, resulting
	// in EOFs at the child's side. This makes `cat` exit, for example.
	close(infd[1]);
	close(outfd[0]);
	if (wait_for_child(NULL)) { return 1; }
	return 0;
}

int32_t do_mknod(dev_t *pDev, char *ifname) {
	char path[256];
	int64_t nbytes = snprintf(path, sizeof(path), "tap-%s", ifname);
	assert(nbytes != -1 && (uint64_t)nbytes < sizeof(path));

	if (mknod(path, S_IFCHR, *pDev)) {
		perror("mknod");
		return 1;
	}
	return 0;
}

// https://github.com/torvalds/linux/blob/3a1e2f4/net/core/dev.c#L1028
bool dev_valid_name(const char *name) {
	if (*name == '\0')
		return false;
	if (strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return false;
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return false;

	while (*name) {
		if (*name == '/' || *name == ':' || isspace(*name))
			return false;
		name++;
	}
	return true;
}

int32_t main(int32_t argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s [interface name] [additional arguments to pass to targetpid discovery]\n", argv[0]);
		return 1;
	}

	char *ifname = argv[1];
	if (dev_valid_name(ifname) == false) {
		fprintf(stderr, "invalid device name\n");
		return 1;
	}

	setnsinfo_t *setns_i = &(setnsinfo_t){ 0 };
	if (open_target_pid_ns_fds(setns_i, argv)) { return 1; }

	dev_t *dev = allocate_dev_shm();
	if (read_majmin_in_netns(ifname, dev, setns_i)) { return 1; }

	if (do_mknod(dev, ifname)) { return 1; }
	munmap(dev, sizeof(*dev));
	return 0;
}

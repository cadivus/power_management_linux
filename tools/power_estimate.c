#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

static pid_t child = -1;

static void
handle_child_exit(int signum)
{
	(void)signum;
	if (child >= 0) {
		char buf[500];
		snprintf(buf, sizeof buf, "/proc/%d/consumed_power", (int)child);
		int fd = open(buf, O_RDONLY);
		if (fd < 0) exit(1);
		ssize_t got = read(fd, buf, sizeof buf);
		write(STDERR_FILENO, buf, got);
		close(fd);
		wait(NULL);
		exit(0);
	}
}

int
main(int argc, const char **argv)
{
	if (argc <= 1) exit(1);
	signal(SIGCHLD, handle_child_exit);
	child = fork();
	switch (child) {
	case -1:
		perror("fork");
		exit(1);

	case 0: // child
		//setpgrp();
		execvp(argv[1], (char *const *)(argv+1));
		perror("exec");
		exit(1);

	default: // parent
		signal(SIGINT, SIG_IGN);
		for (;;) { sleep(1000); }
	}
}

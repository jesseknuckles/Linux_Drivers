#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"
#define MAX_CONCURRENCY 20

/* Command-line option for concurrency */
static int g_concurrency = 0;

static void usage(const char *cmd) {
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  p <int>    Use <int> processes to concurrently consume data\n"
	       "                  MIN: 1, MAX: %d\n"
	       "  h          Print this message\n",
	       cmd, MAX_CONCURRENCY);
}

static int do_procs(int fd) {
	int i, status, ret = 0;
	pid_t pid;
	char* buf; //cuz I'm dynamically allocating buffer
	int count; //counts size of message read from queue

	for(i = 0; i < g_concurrency; i++) {
		pid = fork();
		if(pid == 0) {
			size_t max_size;
			max_size = ioctl(fd, SCULL_IOCGETELEMSZ); //gives maximum size a message can be that's getting read from queue
			buf = (char*) malloc(max_size); //allocate buf on heap
			if((count = read(fd, buf, max_size)) < 0) { //read from /dev/scull via driver and queue
				free(buf); //must free cuz function returns
				perror("read");//prints error message
				exit(-1);
			}
			if(count) {
				//will give issues if count == max_size
				buf[count] = '\0'; //adds null terminator to message
				printf("read: %s\n", buf); //print message
				free(buf); //free buf
			}

			exit(EXIT_SUCCESS);
		} else if(pid < 0) {
			perror("cannot fork more children");
			ret = -1;
			break;
		}
	}
	while(i-- > 0) {
		wait(&status);
	}

	return ret;
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv) {
	cmd_t cmd;

	if(argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch(cmd) {
	case 'p':
		if(argc < 3) {
			fprintf(stderr, "%s: Missing concurrency\n", argv[0]);
			cmd = -1;
			break;
		}
		g_concurrency = atoi(argv[2]);
		if(g_concurrency < 1 || g_concurrency > MAX_CONCURRENCY) {
			fprintf(stderr, "%s: Invalid value (%d) for "
					"concurrency\n", 
					argv[0], g_concurrency);
			cmd = -1;
			break;
		}
		break;
	
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if(cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}

static int do_op(int fd, cmd_t cmd) {
	int ret;

	switch(cmd) {
	case 'p':
		ret = do_procs(fd);
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if(ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv) {
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if(fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if(close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}

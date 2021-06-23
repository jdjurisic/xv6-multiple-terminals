#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user.h"

int test1(void)
{
	printf("\nstarting test 1\n");
	if(fork())
	{
		wait();
		int fd = shm_open("/test1");
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		sleep(50);
		if(p[0] == 42 && p[1] == 42)
		{
			printf("Test 1 OK (if no other errors appeared)\n");
		}
		else
		{
			printf("Test 1 Not OK\n");
		}
		shm_close(fd);
		return 0;
	}

	if(fork())
	{
		int fd = shm_open("/test1");
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		p[0] = 42;
		shm_close(fd);
		wait();		
	}
	else
	{
		int fd = shm_open("/test1");
		int size = shm_trunc(fd, 400);
		int *p;
		shm_map(fd, (void **) &p, O_RDWR);
		p[1] = 42;
		shm_close(fd);
		
	}
	return 1;
}

int test2(void)
{
	printf("\nstarting test 2\n");
	int fd = shm_open("/test2");
	int size = shm_trunc(fd, 400);
	int *p;
	shm_map(fd, (void **) &p, O_RDWR);
	if(fork())
	{
		wait();
		if(p[0] == 42 && p[1] == 42)
		{
			printf("Test 2 OK (if no other errors appeared)\n");
		} 
		shm_close(fd);
		return 0;
	}

	if(fork())
	{
		p[0] = 42;
		shm_close(fd);
		wait();
	}
	else
	{
		p[1] = 42;
		shm_close(fd);
		
	}
	return 1;
}

int test3(void)
{
	printf("\nstarting test 3\n");
	int fd = shm_open("/test3");
	int pid;
	int size = shm_trunc(fd, 400);
	int *p;
	shm_map(fd, (void **) &p, O_RDONLY);
	if(pid = fork())
	{
		wait();
		printf("Test 3 OK (if trap 14 was triggered before this by proces with pid: %d)\n", pid);
		shm_close(fd);
		return 0;
	}

	printf("Triggering trap 14!\n");
	p[1] = 42;
	shm_close(fd); //this doesent get called, cleanup elsewhere on crash
	return 1;
}

int
main(int argc, char *argv[])
{
	if(test1()) goto ex;
	if(test2()) goto ex;
	if(test3()) goto ex;

ex:
	exit();
}

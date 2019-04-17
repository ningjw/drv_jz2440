#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
int fd;
void signal_fun(int signum)
{
	unsigned char key_val[4];
	read(fd, key_val, 4);
	printf("key_val = 0x%02x,0x%02x,0x%02x,0x%02x\n", key_val[0],key_val[1],key_val[2],key_val[3]);
}

int main(int argc, char **argv)
{
	unsigned char key_val;
	int ret;
	int Oflags;

	signal(SIGIO, signal_fun);
	fd = open("/dev/keys", O_RDWR | O_NONBLOCK);
	if (fd < 0){
		printf("can't open!\n");
	}
	fcntl(fd, F_SETOWN, getpid());//设置异步I/O所有权
	Oflags = fcntl(fd, F_GETFL); //获得文件状态标记
	fcntl(fd, F_SETFL, Oflags | FASYNC);//设置文件状态标记

	while (1)
	{
		sleep(1000);
	}	return 0;
}
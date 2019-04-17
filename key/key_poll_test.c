#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
 
int main(int argc, char **argv)
{
	unsigned char key_val[4];
	int ret;
	struct pollfd fds;
	fds.fd = open("/dev/keys",O_RDWR);
	if (fds.fd < 0){
		printf("can't open!\n");
	}
	fds.events = POLLIN;
	while (1)
	{
		ret = poll(&fds, 1, 5000);//需要监视的设备数为5秒
		if (ret == 0){
			printf("time out\n");
		}else{
			read(fds.fd, &key_val, 4);
			printf("key_val = 0x%2x,0x%2x,0x%2x,0x%2x\n", key_val[0],key_val[1],key_val[2],key_val[3]);
		}
	}
	return 0;
}
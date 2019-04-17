#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc,char **argv)
{
	int fd;
	char led_val[2];
	int i,status = 0;
	fd = open("/dev/leds",O_RDWR);
	if (fd < 0){
		printf("can't open leds\n");
	} 
	
	while(1){
		/* code */
		for(i=1; i<=3; i++){
			led_val[0] = i;
			led_val[1] = status & 0x1;
			write(fd, led_val, 2);
			usleep(100000);
		}
		status = ~status;
	}
	
	return 0;
}

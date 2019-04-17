#include <linux/module.h>  //定义了THIS_MODULE宏
#include <linux/fs.h>      //定义了file_operations结构体
#include <asm/uaccess.h>   //定义了copy_to_user函数
#include <asm/io.h>        //定义了ioremap 与iounremap函数
#include <linux/device.h>  //定义了class_create/device_create/class_destory/device_destory函数
                           //定义了class 与 class_device结构体
#include <mach/regs-gpio.h>//包含了GPIO相关宏
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数
#define LED_NUM 3
static struct class *leddrv_class;
static struct device	*leddrv_class_dev;

/**应用程序的open函数时,最终会调用该函数,配置LED控制引脚为输出*/
static int led_drv_open(struct inode *inode, struct file *file)
{
	s3c2410_gpio_cfgpin(S3C2410_GPF(4),S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_cfgpin(S3C2410_GPF(5),S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_cfgpin(S3C2410_GPF(6),S3C2410_GPIO_OUTPUT);
	return 0;
}

/**应用程序的read函数时,最终会调用该函数*/
ssize_t led_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	unsigned char led_vals[LED_NUM];
	if (size > LED_NUM || size < 0)
		return -EINVAL;
	led_vals[0] = s3c2410_gpio_getpin(S3C2410_GPF(4));
	led_vals[1] = s3c2410_gpio_getpin(S3C2410_GPF(5));
	led_vals[2] = s3c2410_gpio_getpin(S3C2410_GPF(6));
	copy_to_user(buf, led_vals, size);
	return size;
}

/**
 * 应用程序的write函数时,最终会调用该函数,调用一次只能控制一个led灯的两灭
 * buf:第一个byte表示控制第几个led灯范围0~3(0表示控制所有灯), 第2个byte取值1=亮灯,0=灭灯
 * size:只能等于2
 */
ssize_t led_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	unsigned char led_vals[2];
	unsigned char value = 0,led_num;
	if ( size != 2)//
			return -EINVAL;

	copy_from_user(led_vals, buf, size);
	value = led_vals[1] ? 0: 1;
	led_num = led_vals[0];
	if(led_num == 0){//同时控制所有led
		s3c2410_gpio_setpin(S3C2410_GPF(4),value);
		s3c2410_gpio_setpin(S3C2410_GPF(5),value);
		s3c2410_gpio_setpin(S3C2410_GPF(6),value);
	}else{//单独控制单个led
		s3c2410_gpio_setpin(S3C2410_GPF(3+led_num),value);
	}
	return 0;
}

static struct file_operations led_drv_fops = {
    .owner  =   THIS_MODULE,
    .open   =   led_drv_open,
	.read	=	led_drv_read,
	.write  =   led_drv_write,
};


int major;
dev_t led_dev;
/**
 *初始化函数在加载该模块时就会调用,
 *注册设备
 */
static int led_drv_init(void)
{
	// alloc_chrdev_region()
	major = register_chrdev(0, "led_drv", &led_drv_fops);
	leddrv_class = class_create(THIS_MODULE, "led_drv");
	if(IS_ERR(leddrv_class))
		return PTR_ERR(leddrv_class);

	/* 加载模块后,/dev目录下会新增 leds */
	leddrv_class_dev = device_create(leddrv_class, NULL, MKDEV(major, 0), NULL, "leds");
	 if(unlikely(IS_ERR(leddrv_class_dev)))
	  return PTR_ERR(leddrv_class_dev);

	printk("led_drv_init\n");
	return 0;
}

static void led_drv_exit(void)
{
	unregister_chrdev(major, "led_drv");
	device_destroy(leddrv_class, MKDEV(major, 0));//卸载设备
	class_destroy(leddrv_class);//删除class类
}

module_init(led_drv_init);
module_exit(led_drv_exit);
MODULE_LICENSE("GPL");

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>   //定义了copy_to_user函数
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <mach/regs-gpio.h>//包含了GPIO相关宏
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数

static int major;
#define DEVICE_NAME "LED_DRV"
static struct class *led_class;
static struct device *led_class_dev;
static int leds_pin[3];

//配置引脚为输出
static int led_open (struct inode *node, struct file *file)
{
	s3c2410_gpio_cfgpin(leds_pin[0],S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_cfgpin(leds_pin[1],S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_cfgpin(leds_pin[2],S3C2410_GPIO_OUTPUT);
	return 0;
}

/**
 * 应用程序的write函数时,最终会调用该函数,调用一次只能控制一个led灯的两灭
 * buf:第一个byte表示控制第几个led灯范围0~3(0表示控制所有灯), 第2个byte取值1=亮灯,0=灭灯
 * size:只能等于2
 */
static ssize_t led_write (struct file *file, const char __user *buf, size_t size, loff_t *loff)
{
	unsigned char led_vals[2];
	unsigned char value = 0,led_num;
	if ( size != 2)//
			return -EINVAL;

	if(copy_from_user(led_vals, buf, size)){
		printk("led_write:copy_from_user error\n");
	}
	value = led_vals[1] ? 0: 1;
	led_num = led_vals[0];
	if(led_num == 0){//同时控制所有led
		s3c2410_gpio_setpin(leds_pin[0],value);
		s3c2410_gpio_setpin(leds_pin[1],value);
		s3c2410_gpio_setpin(leds_pin[2],value);
	}else if(led_num <=3 ){//单独控制单个led
		s3c2410_gpio_setpin(leds_pin[led_num-1],value);
	}
	return 0;
}


const struct file_operations  led_fops =
{
	.owner = THIS_MODULE,
	.open  = led_open,
	.write = led_write,
};

static int led_probe(struct platform_device * pdev)
{
	//根据platform_device的资源,获取led对应的io资源
	struct resource *res;
	unsigned char i = 0;
	for(i = 0; i< pdev->num_resources; i++){
		res = platform_get_resource(pdev, IORESOURCE_IO, i);
    	leds_pin[i] = res->start;
	}

	//注册字符设备驱动程序
	printk(">>led_probe\n");

	major = register_chrdev(major, DEVICE_NAME, &led_fops);
	led_class = class_create(THIS_MODULE, DEVICE_NAME);
    if(IS_ERR(led_class))
		return PTR_ERR(led_class);
	/* 加载模块后,/dev目录下会新增 leds */
	led_class_dev = device_create(led_class, NULL, MKDEV(major,0), NULL, "leds");
    if(unlikely(IS_ERR(led_class_dev)))
	  return PTR_ERR(led_class_dev);

	return 0;
}

int led_remove(struct platform_device *pdev)
{
	//卸载字符设备驱动程序
	printk(">>led_remove\n");
	unregister_chrdev(major, DEVICE_NAME);
	device_destroy(led_class, MKDEV(major,0));
	class_destroy(led_class);
	return 0;
}

struct platform_driver led_drv = {
	.probe = led_probe,
	.remove = led_remove,
	.driver = {.name = "leds",}//一定需要同名,才会调用probe函数
};


static int led_drv_init(void)
{
	platform_driver_register(&led_drv);
	return 0;
}

static void led_drv_exit(void)
{
	platform_driver_unregister(&led_drv);
}

module_init(led_drv_init);
module_exit(led_drv_exit);
MODULE_LICENSE("GPL");
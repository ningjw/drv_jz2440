#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数
#include <linux/module.h>

//定义一个led资源文件
static struct resource led_resource[] = {
    [0] = {
        .start = S3C2410_GPF(4),
        .end   = S3C2410_GPF(4),
        .flags = IORESOURCE_IO,
    },
    [1] = {
        .start = S3C2410_GPF(5),
        .end   = S3C2410_GPF(5),
        .flags = IORESOURCE_IO,
    },
    [2] = {
        .start = S3C2410_GPF(6),
        .end   = S3C2410_GPF(6),
        .flags = IORESOURCE_IO,
    },
};


static void led_release(struct device * dev)
{
    //该函数为防止写在设备时出错
}

static struct platform_device led_device = {
    .name          = "leds",
    .id            = -1,
    .num_resources = ARRAY_SIZE(led_resource),
    .resource      = led_resource,
    .dev           = {.release = led_release},
};

static int led_init(void)
{
	platform_device_register(&led_device);
	return 0;
}

static void led_exit(void)
{
	platform_device_unregister(&led_device);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
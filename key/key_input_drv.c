#include <linux/module.h>  //定义了THIS_MODULE宏
#include <linux/fs.h>      //定义了file_operations结构体
#include <linux/device.h>  //定义了class_create/device_create/class_destory/device_destory函数
                           //定义了class 与 class_device结构体
#include <linux/interrupt.h>//定义了IRQF_TRIGGER_RISING 宏
#include <linux/input.h>   //定义了按键相关
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数
#include <asm/uaccess.h>   //定义了copy_to_user函数
#include <asm/io.h>        //定义了ioremap 与iounremap函数
#include <mach/regs-gpio.h>//包含了GPIO相关宏
#include <mach/irqs.h>     //包含了中断相关定义

struct pin_desc_s{
	int irq;	//按键的外部中断标志位, 可在irqs.h中查找有那些中断
	char *name;	//名称,一段字符串,自己定义.
	unsigned int pin;//引脚
	unsigned int key_val;//键值,键盘上每个按键(0~9,a~z...)都有一个对应的键值,用于判断按下了那个键.
};

struct pin_desc_s pins_desc[4] = {
	{IRQ_EINT0,  "S2", S3C2410_GPF(0),  KEY_L},
	{IRQ_EINT2,  "S3", S3C2410_GPF(2),  KEY_S},
	{IRQ_EINT11, "S4", S3C2410_GPG(3),  KEY_ENTER},
	{IRQ_EINT19, "S5", S3C2410_GPG(11), KEY_LEFTSHIFT},
};

static struct input_dev *keys_dev;
static struct pin_desc_s *irq_pd;//在中断发生时,用于保存现场.
static struct timer_list keys_timer;

/**
 * 超时处理函数
 */
void keys_timer_handler(unsigned long data)
{
	unsigned int pinval;

	if(NULL == irq_pd){
		return;
	}
	pinval = s3c2410_gpio_getpin(irq_pd->pin);
	if (pinval){/* 松开 : 最后一个参数: 0-松开, 1-按下 */
		input_event(keys_dev, EV_KEY, irq_pd->key_val, 0);
		input_sync(keys_dev);
	}
	else{/* 按下 */
		input_event(keys_dev, EV_KEY, irq_pd->key_val, 1);
		input_sync(keys_dev);
	}
}

/**
 * 按键中断处理函数
 */
static irqreturn_t key_irq_handler(int irq, void *dev_id)
{
	irq_pd = (struct pin_desc_s *)dev_id;//保存发生中断的引脚相关信息,以便在超时处理函数中使用.
	//jiffies记录自系统启动一来产生的节拍数
	//HZ-1s，HZ/100 = 10ms
	mod_timer(&keys_timer, jiffies+HZ/100);//修改定时器，10ms后启动定时器
	return IRQ_RETVAL(IRQ_HANDLED);
}

static int key_drv_init(void)
{
	unsigned char i = 0;
	//分配,设置,注册一个input_dev结构体
	keys_dev = input_allocate_device();
	set_bit(EV_KEY, keys_dev->evbit);  //设置能产生按键类事件
	set_bit(EV_REP, keys_dev->evbit);  //设置键盘重复按键类事件
	set_bit(KEY_L, keys_dev->keybit);  //支持L按键
	set_bit(KEY_S, keys_dev->keybit);  //支持按键S
	set_bit(KEY_ENTER, keys_dev->keybit);//支持按键enter
	set_bit(KEY_LEFTSHIFT, keys_dev->keybit);//支持按键左边的shift
	input_register_device(keys_dev);//注册
	
	//初始化定时器，防抖动
	init_timer(&keys_timer);
	keys_timer.function = keys_timer_handler;//定时器的处理函数
	add_timer(&keys_timer);

	/* 注册四个中断 */
	for (i = 0; i < 4; i++){
		request_irq(
				pins_desc[i].irq,//中断号
				key_irq_handler,//中断处理函数
				(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING),//触发中断的条件
				pins_desc[i].name,//中断名,自己定义的一段字符串
				&pins_desc[i]);//发生中断时,该指针会传递给中断函数.
	}
	return 0;
}

static void key_drv_exit(void)
{
	int i;
	for (i = 0; i < 4; i++){
		free_irq(pins_desc[i].irq, &pins_desc[i]);
	}
	del_timer(&keys_timer);
	input_unregister_device(keys_dev);
	input_free_device(keys_dev);
}

module_init(key_drv_init);
module_exit(key_drv_exit);
MODULE_LICENSE("GPL");

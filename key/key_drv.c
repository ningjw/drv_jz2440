#include <linux/module.h>  //定义了THIS_MODULE宏
#include <linux/fs.h>      //定义了file_operations结构体
#include <linux/device.h>  //定义了class_create/device_create/class_destory/device_destory函数
                           //定义了class 与 class_device结构体
#include <linux/interrupt.h>//定义了IRQF_TRIGGER_RISING 宏
#include <linux/input.h>   //定义了按键相关
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数
#include <asm/uaccess.h>   //定义了copy_to_user函数
#include <asm/io.h>        //定义了ioremap 与iounremap函数
#include <mach/regs-gpio.h>//包含了GPIO相关宏
#include <mach/irqs.h>     //包含了中断相关定义
#include <linux/types.h>
#include <linux/spinlock.h>

#define DEVICE_NAME "key_drv"
static int major;
static struct class *key_class;
static struct device *key_class_dev;
static DECLARE_WAIT_QUEUE_HEAD(key_waitq);//定义队列
static volatile unsigned char ev_press;
static struct fasync_struct *key_async;
static struct timer_list keys_timer;
static struct semaphore sem;//定义信号量

typedef struct {
	int irq;	//按键的外部中断标志位, 可在irqs.h中查找有那些中断
	char *name;	//名称,一段字符串,自己定义.
	unsigned int pin;//引脚
	unsigned int key_val;//键值,键盘上每个按键(0~9,a~z...)都有一个对应的键值,用于判断按下了那个键.
}key_desc_t;
static key_desc_t *keydesc = NULL;
static key_desc_t key_desc[4] = {
	{IRQ_EINT0,"s2",S3C2410_GPF(0),0x2},
	{IRQ_EINT2,"s3",S3C2410_GPF(2),0x3},
	{IRQ_EINT11,"s4",S3C2410_GPG(3),0x4},
	{IRQ_EINT19,"s5",S3C2410_GPG(11),0x5},
};

/**
 * 超时处理函数
 */
void keys_timer_handler(unsigned long data)
{
	unsigned int i,pin_v;

	for(i=0; i<4; i++){
		pin_v = s3c2410_gpio_getpin(key_desc[i].pin);
		if(pin_v == 0){//低电平，按键按下
			key_desc[i].key_val |= 0x80;//最高位置一
		}else{//高电平，没有按键按下
			key_desc[i].key_val &= ~0x80;//最高位清零
		}
	}
	
	ev_press = 1;
	wake_up_interruptible(&key_waitq);/* 唤醒休眠的进程 */ 

	/* 用kill_fasync函数告诉应用程序，有数据可读了  
     * button_fasync结构体里包含了发给谁(PID指定) 
     * SIGIO表示要发送的信号类型 
     * POLL_IN表示发送的原因(有数据可读了) 
     */
	kill_fasync(&key_async, SIGIO, POLL_IN);
}

/*中断处理函数，当发生中断时会调用该函数*/
static irqreturn_t irq_handler(int irq, void *dev)
{
	//jiffies记录自系统启动一来产生的节拍数
	//HZ-1s，HZ/100 = 10ms
	mod_timer(&keys_timer, jiffies+msecs_to_jiffies(10));//修改定时器，10ms后产生定时器超时中断

	return IRQ_RETVAL(IRQ_HANDLED);
}

static ssize_t key_read (struct file *file, char __user *buf, size_t size, loff_t *loff)
{
	unsigned char key_v[4],i;
	if(buf == NULL ){
		return -EINVAL;
	}

	/*没有任何按键操作时，ev_press = 0，进入休眠
	 *当有按键按下时，ev_press = 1，不休眠
	*/
	wait_event_interruptible(key_waitq,ev_press);

	for(i = 0;i<4; i++){
		key_v[i] = key_desc[i].key_val;
	}
	if(copy_to_user(buf, key_v, 4)){
		printk("copy_to_user error");
	}
	ev_press = 0;//清零
	return 4;
}

/** 注册中断,按键检测引脚默认为高电平，当有按键按下时为低电平*/
static int key_open (struct inode *inode, struct file *file)
{
	int i = 0;
	int ret;
	if (file->f_flags & O_NONBLOCK){
		if (down_trylock(&sem))//获取不到信号时立即返回
			return -EBUSY;
	}else{
		down(&sem);//获取不到信号时，等待信号返回
	}
	for(i = 0;i<4;i++){
		ret = request_irq(key_desc[i].irq,//中断号,可在irqs.h中查看
		            irq_handler,//中断处理函数
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,//中断触发条件，可在interrupt.h中查看
					key_desc[i].name,//中断名,自己定义的一段字符串
					&key_desc[i]);//发生中断时,该指针会传递给中断函数.
		if(ret){
			printk("request_irq error,ret = %d",ret);
		}
	}
	return 0;
}

static unsigned int key_poll (struct file *file, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	/* 将进程挂在key_waitq队列上 */  
	poll_wait(file, &key_waitq, wait);

	/* 当没有按键按下时，即不会进入按键中断处理函数，此时ev_press = 0  
     * 当按键按下时，就会进入按键中断处理函数，此时ev_press被设置为1 */
	if(ev_press == 1){
		mask |= POLLIN | POLLRDNORM;/* 表示有普通数据可读 */  
	}

	/* 有按键按下时，mask |= POLLIN | POLLRDNORM, 系统会立即返回用户空间
	   没有按键按下时，mask = 0 ，系统会在内核空间进入休眠*/  
	return mask;
}

static int key_release (struct inode *inode, struct file *file)
{
	int i = 0;
	for(i = 0;i<4;i++){
		free_irq(key_desc[i].irq,&key_desc[i]);
	}
	up(&sem);
	return 0;
}

/* 当应用程序调用了fcntl(fd, F_SETFL, Oflags | FASYNC);  
 * 则最终会调用驱动的fasync函数，在这里则是fifth_drv_fasync 
 * fifth_drv_fasync最终又会调用到驱动的fasync_helper函数 
 * fasync_helper函数的作用是初始化/释放fasync_struct 
 */ 
static int key_fasync (int fd, struct file *filp, int on)
{
	printk(">>key_fasync\n");
	return fasync_helper(fd, filp, on, &key_async);//key_async初始化
}


static struct file_operations key_fops ={
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.release = key_release,
	.poll = key_poll,
	.fasync = key_fasync
};


/*入口函数*/
static int __init key_drv_init(void)
{
    major = register_chrdev(0, DEVICE_NAME, &key_fops);
	key_class = class_create(THIS_MODULE, DEVICE_NAME);
	if(IS_ERR(key_class))
		return PTR_ERR(key_class);

	key_class_dev = device_create(key_class, NULL, MKDEV(major,0), NULL, "keys");// /dev/key
	if(unlikely(IS_ERR(key_class_dev)))
	  return PTR_ERR(key_class_dev);
	
	//初始化定时器，防抖动
	init_timer(&keys_timer);
	keys_timer.function = keys_timer_handler;//定时器的处理函数
	add_timer(&keys_timer);

	//初始化信号量，同一时刻最多只能有1个应用打开程序。
	sema_init(&sem,1);

	// s3c2410_gpio_cfgpin(key_desc[0].pin,S3C2410_GPF0_EINT0);
	// s3c2410_gpio_cfgpin(key_desc[1].pin,S3C2410_GPF2_EINT2);
	// s3c2410_gpio_cfgpin(key_desc[2].pin,S3C2410_GPG3_EINT11);
	// s3c2410_gpio_cfgpin(key_desc[3].pin,S3C2410_GPG11_EINT19);
	return 0;
}
/*出口函数*/
static void __exit key_drv_exit(void)
{
    unregister_chrdev(major, DEVICE_NAME);
	device_destroy(key_class, MKDEV(major, 0));
	class_destroy(key_class);
	del_timer(&keys_timer);
}

module_init(key_drv_init);
module_exit(key_drv_exit);
MODULE_LICENSE("GPL");

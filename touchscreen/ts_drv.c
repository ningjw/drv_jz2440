#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/map.h>

#include <mach/regs-gpio.h>//包含了GPIO相关宏
#include <plat/adc.h>
#include <plat/regs-adc.h>
#include <plat/ts.h>

struct touch_regs{
	unsigned long adccon;
	unsigned long adctsc;
	unsigned long adcdly;
	unsigned long adcdat0;
	unsigned long adcdat1;
	unsigned long adcupdn;
};

static struct input_dev *touch_dev;
static volatile struct touch_regs* ts_regs;
static struct timer_list touch_timer;

/*进入等待中断模式：
*等待触摸笔按下中断，开始手动测量X坐标，电压从YM端点进行检测
*/
static void enter_wait_down_mode(void)
{
	//检测笔尖抬起中断|YM输出使能|YP输出禁止|XP输出禁止|手动测量X方向
	ts_regs->adctsc = S3C2410_ADCTSC_YM_SEN | S3C2410_ADCTSC_YP_SEN | 
	                  S3C2410_ADCTSC_XP_SEN | S3C2410_ADCTSC_XY_PST(3);
}

/*进入等待中断模式：
*等待触摸笔松开中断，开始手动测量X坐标，电压从YM端点进行检测
*/
static void enter_wait_up_mode(void)
{
	//检测笔尖抬起中断|YM输出使能|YP输出禁止|XP输出禁止|手动测量X方向
	ts_regs->adctsc = S3C2443_ADCTSC_UD_SEN | S3C2410_ADCTSC_YM_SEN |
	                  S3C2410_ADCTSC_YP_SEN | S3C2410_ADCTSC_XP_SEN | 
					  S3C2410_ADCTSC_XY_PST(3);
}

/*进入自动测量xy模式
*自动转换xy模式：x，y都转换完成后才产生ADC中断
*分离xy转换模式：x转换完成产生一次中断，y转换完成产生一次中断
*/
static void enter_measure_xy_mode(void)
{
	//XP上拉禁止 | 自动顺序x方向和Y方向测量
	ts_regs->adctsc = S3C2410_ADCTSC_PULL_UP_DISABLE | S3C2410_ADCTSC_AUTO_PST;
}

static void start_adc(void)
{
	//时能ADC转换启动，且此位在启动后自动清0
	ts_regs->adccon |= S3C2410_ADCCON_ENABLE_START;
}

static int s3c_filter_ts(int x[], int y[])
{
#define ERR_LIMIT 10

	int avr_x, avr_y;
	int det_x, det_y;

	avr_x = (x[0] + x[1])/2;
	avr_y = (y[0] + y[1])/2;

	det_x = (x[2] > avr_x) ? (x[2] - avr_x) : (avr_x - x[2]);
	det_y = (y[2] > avr_y) ? (y[2] - avr_y) : (avr_y - y[2]);

	if ((det_x > ERR_LIMIT) || (det_y > ERR_LIMIT))
		return 0;

	avr_x = (x[1] + x[2])/2;
	avr_y = (y[1] + y[2])/2;

	det_x = (x[3] > avr_x) ? (x[3] - avr_x) : (avr_x - x[3]);
	det_y = (y[3] > avr_y) ? (y[3] - avr_y) : (avr_y - y[3]);

	if ((det_x > ERR_LIMIT) || (det_y > ERR_LIMIT))
		return 0;
	
	return 1;
}


static void touch_timer_function(unsigned long data)
{
	if(ts_regs->adcdat0 & S3C2410_ADCDAT1_UPDOWN )//触摸笔抬起
	{
		input_report_abs(touch_dev, ABS_PRESSURE, 0);
		input_report_key(touch_dev, BTN_TOUCH, 0);
		input_sync(touch_dev);
		enter_wait_down_mode();
	}
	else  //触摸笔按下
	{
		//测量xy坐标
		enter_measure_xy_mode();
		start_adc();
	}
}

/*触摸笔按下，松开中断处理函数*/
static irqreturn_t touch_irq_handler(int irq, void *dev_id)
{
	if(ts_regs->adcdat0 & S3C2410_ADCDAT1_UPDOWN  )//触摸比抬起
	{
		enter_wait_down_mode();
	}
	else//触摸比按下
	{
		enter_measure_xy_mode();
		start_adc();
	}
	
	return IRQ_HANDLED;
}

static irqreturn_t adc_irq_handler(int irq, void *dev_id)
{
	static int cnt = 0;
	int adcdat0,adcdat1;
	static int x[4],y[4];
	
	/*优化2: 如果ADC完成时,发现触摸笔已经松开, 则丢弃本次结果*/
	adcdat0 = ts_regs->adcdat0;
	adcdat1 = ts_regs->adcdat1;
	if(ts_regs->adcdat0 & S3C2410_ADCDAT1_UPDOWN)//触摸笔抬起
	{
		//Stylus up state
		cnt = 0;
		input_report_abs(touch_dev, ABS_PRESSURE, 0);
		input_report_key(touch_dev, BTN_TOUCH, 0);
		input_sync(touch_dev);
		enter_wait_down_mode();
	}
	else//触摸笔按下
	{
		/*优化3:多次测量,求平均值*/
		x[cnt] = ts_regs->adcdat0 & 0x3ff;
		y[cnt] = ts_regs->adcdat1 & 0x3ff;
		++cnt;
		if(cnt >= 4)
		{
			/*优化4:软件过滤*/
			if(s3c_filter_ts(x,y))
			{
				//printk(" x=%d, y=%d \n",  (x[0]+x[1]+x[2]+x[3])/4, (y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(touch_dev, ABS_X, (x[0]+x[1]+x[2]+x[3])/4);
				input_report_abs(touch_dev, ABS_Y, (y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(touch_dev, ABS_PRESSURE, 1);
				input_report_key(touch_dev, BTN_TOUCH, 1);
				input_sync(touch_dev);
			}
			cnt = 0;//为了能够处理滑动事件，
			enter_wait_up_mode();
			mod_timer(&touch_timer, jiffies + HZ/100);
		}
		else
		{
			enter_measure_xy_mode();
			start_adc();
		}
	}
	
	return IRQ_HANDLED;
}


static int touch_init(void)
{
	struct clk*  clk;
    int ret;
	//1.分配一个input_device结构体
	touch_dev = input_allocate_device();
	
	//2.设置
	//2.1 事件类型: 设置evbit
	set_bit(EV_KEY, touch_dev->evbit);//按键事件
	set_bit(EV_ABS, touch_dev->evbit);//绝对位移事件
	//2.2 具体事件
	set_bit(BTN_TOUCH ,touch_dev->keybit);
	input_set_abs_params(touch_dev, ABS_X, 0, 0x3FF, 0, 0);
	input_set_abs_params(touch_dev, ABS_Y, 0, 0x3FF, 0, 0);
	input_set_abs_params(touch_dev, ABS_PRESSURE, 0, 1, 0, 0);
	
	//3.注册
	ret = input_register_device(touch_dev);
	if(ret < 0){
        printk("failed to register input device\n");
		ret = -EIO;
        return ret;
    }

	//4.硬件相关
	//4.1 使能时钟CLKCON
	clk = clk_get(NULL, "adc");
	clk_enable(clk);
	//4.2 设置相关寄存器
	ts_regs = ioremap(S3C24XX_PA_ADC, sizeof(struct touch_regs));
    /*bit[14]:PRSCEN = 1 //预分频使能
	*bit[13:6]:PRSCVL =49 //预分频系数, ADCCLK=PCLK/(49+1) = 1MHz
	*bit[0]: ENABLE_START = 0 //A/D conversion starts by enable
	*/
	ts_regs->adccon = S3C2410_ADCCON_PRSCEN | S3C2410_ADCCON_PRSCVL(49);
   	ret = request_irq(IRQ_TC, touch_irq_handler, IRQF_SAMPLE_RANDOM, "touch_pen", NULL);//注册一个触摸中断
   	if (ret) {
        printk("request IRQ_TC error\n");
        return ret;
    }
    ret = request_irq(IRQ_ADC, adc_irq_handler, IRQF_SAMPLE_RANDOM, "adc", NULL);
    if (ret) {
        printk("request IRQ_TC error\n");
        return ret;
    }
	
	/*优化1:设置ADCDLY为最大值*/
	ts_regs->adcdly = 0xFFFF;

	/*优化5:使用定时器处理长按滑动*/
	init_timer(&touch_timer);
	touch_timer.function = touch_timer_function;
	add_timer(&touch_timer);
	
	enter_wait_down_mode(); 
	return 0;
}

static void touch_exit(void)
{
	free_irq(IRQ_TC, NULL);
	free_irq(IRQ_ADC, NULL);
	input_unregister_device(touch_dev);
	input_free_device(touch_dev);
	iounmap(ts_regs);
	del_timer(&touch_timer);
//	clk_disable(clk);
}

module_init(touch_init);
module_exit(touch_exit);

MODULE_LICENSE("GPL");
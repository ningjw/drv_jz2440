#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>    //包含了s3c2410_gpio_cfgpin等io操作函数
#include <mach/regs-lcd.h>
#include <mach/regs-gpio.h>
#include <mach/fb.h>

#define LCD_LENGTH 480
#define LCD_WIDTH  272
#define BIT_WIDTH  32

static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info);
			     

static struct fb_info *lcd;
static struct fb_ops  lcd_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg = s3c_lcdfb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

struct lcd_regs{
	unsigned long	lcdcon1;
	unsigned long	lcdcon2;
	unsigned long	lcdcon3;
	unsigned long	lcdcon4;
	unsigned long	lcdcon5;
    unsigned long	lcdsaddr1;
    unsigned long	lcdsaddr2;
    unsigned long	lcdsaddr3;
};

static volatile unsigned long *gpccon;
static volatile unsigned long *gpdcon;
static volatile unsigned long *gpgcon;
static volatile struct lcd_regs* lcd_regs;
static u32 pseudo_palette[16];


/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}


static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info)
{
	unsigned int val;
	
	if (regno > 16)
		return 1;

	/* 用red,green,blue三原色构造出val */
	val  = chan_to_field(red,	&info->var.red);
	val |= chan_to_field(green, &info->var.green);
	val |= chan_to_field(blue,	&info->var.blue);
	
	//((u32 *)(info->pseudo_palette))[regno] = val;
	pseudo_palette[regno] = val;
	return 0;
}

static int lcd_init(void)
{
 	//1.分配一个fb_info结构体
	lcd = framebuffer_alloc(0, NULL);

	//2.设置
	//2.1设置固定的参数
	strcpy(lcd->fix.id, "mylcd");
	lcd->fix.smem_len = LCD_LENGTH*LCD_WIDTH*BIT_WIDTH/8; /* TQ2440的LCD位宽是24,但是2440里会分配4字节即32位(浪费1字节) */
	lcd->fix.type     = FB_TYPE_PACKED_PIXELS;
	lcd->fix.visual   = FB_VISUAL_TRUECOLOR;
	lcd->fix.line_length = LCD_LENGTH*BIT_WIDTH/8;
	
	//2.2设置可变的参数
	lcd->var.xres           = LCD_LENGTH;
	lcd->var.yres           = LCD_WIDTH;
	lcd->var.xres_virtual   = LCD_LENGTH;
	lcd->var.yres_virtual   = LCD_WIDTH;
	lcd->var.bits_per_pixel = BIT_WIDTH;//每个像素用多少位
	
	lcd->var.red.offset     = 16;
	lcd->var.red.length     = 8;
	lcd->var.green.offset   = 8;
	lcd->var.green.length   = 8;
	lcd->var.blue.offset    = 0;
	lcd->var.blue.length    = 8;

	lcd->var.activate       = FB_ACTIVATE_NOW;
	
	//2.3设置操作函数
	lcd->fbops = &lcd_ops;
	
	//2.4其他
	lcd->pseudo_palette = pseudo_palette;//调色板
	lcd->screen_size = LCD_LENGTH*LCD_WIDTH*BIT_WIDTH/8;
	
	//3.硬件相关的设置
	//3.1配置gpio用于lcd
	gpccon = ioremap(0x56000020, 4);
	gpdcon = ioremap(0x56000030, 4);
	gpgcon = ioremap(0x56000060, 4);

	*gpccon = 0xaaaaaaaa;/* GPIO管脚用于VD[7:0],LCDVF[2:0],VM,VFRAME,VLINE,VCLK,LEND */
	*gpdcon = 0xaaaaaaaa;/* GPIO管脚用于VD[23:8] */

	s3c2410_gpio_cfgpin(S3C2410_GPB(0),S3C2410_GPIO_OUTPUT);//(背光驱动使能引脚)gpb0设置为输出
	s3c2410_gpio_setpin(S3C2410_GPB(0),0);//输出低电平

	*gpgcon |= S3C2410_GPG4_LCDPWREN;//gpg4用作lcd电源使能引脚

	lcd_regs = ioremap(S3C2410_PA_LCD, sizeof(struct lcd_regs));

	//3.2根据lcd手册设置lcd控制器
	/*bit[17:8]: VCLK = HCLK / [(CLKVAL+1) * 2];
	*	  (10MHz)VCLK = 100MHz/[(CLKVAL+1) * 2];
	*   => CLKVAL = 4
	* bit[6:5]:display mode = 0b11;
	* bit[4:1]:Select the BPP (Bits Per Pixel) mode.  24 bpp for TFT
	* bit[0]  :LCD video output and the logic enable/disable.*/  
	lcd_regs->lcdcon1  = S3C2410_LCDCON1_CLKVAL(4) | 
	                     S3C2410_LCDCON1_TFT | 
						 S3C2410_LCDCON1_TFT24BPP;

	/*垂直方向的时间参数
	* bit[31:24]:VBPD = 1;//VSYNC之后再过多长时间才能发出第一行数据
	* bit[23:14]:LINEVAL = 319;//多少行
	* bit[13: 6]:VFPD = 1;//发出最后一行数据之后,再过多长时间发出VSYNC信号
	* bit[ 5: 0]:VSPW = 9;//VSYNC信号的脉冲宽度   */
	lcd_regs->lcdcon2 = S3C2410_LCDCON2_VBPD(1) | 
	                    S3C2410_LCDCON2_LINEVAL(271) | 
						S3C2410_LCDCON2_VFPD(1) |
						S3C2410_LCDCON2_VSPW(9);
	/* 水平方向的时间参数
	 * bit[25:19]: HBPD, VSYNC之后再过多长时间才能发出第1行数据
	 *             LCD手册 thb=2
	 *             HBPD=1
	 * bit[18:8]: 多少列, LCD_LENGTH, 所以HOZVAL=LCD_LENGTH-1=479
	 * bit[7:0] : HFPD, 发出最后一行里最后一个象素数据之后，再过多长时间才发出HSYNC
	 *             LCD手册thf=2, 所以HFPD=2-1=1*/
	lcd_regs->lcdcon3 = S3C2410_LCDCON3_HBPD(1) | 
	                    S3C2410_LCDCON3_HOZVAL(479) | 
						S3C2410_LCDCON3_HFPD(1);

	/* 水平方向的同步信号
	* bit[7:0] : HSPW, HSYNC信号的脉冲宽度, LCD手册Thp=41, 所以HSPW=41-1=40*/ 
	lcd_regs->lcdcon4 = S3C2410_LCDCON4_HSPW(40);

	/* 信号的极性 
	 * bit[11]: 1=565 format, 对于24bpp这个不用设
	 * bit[10]: 0 = 在VCLK的下降沿取数据
	 * bit[9] : 1 = HSYNC信号要反转,即低电平有效 
	 * bit[8] : 1 = VSYNC信号要反转,即低电平有效 
	 * bit[6] : 0 = VDEN不用反转
	 * bit[3] : 0 = PWREN输出0
	 * BSWP = 0, HWSWP = 0, BPP24BL = 0 : 当bpp=24时,2440会给每一个象素分配32位即4字节,哪一个字节是不使用的? 看2440手册P412
         * bit[12]: 0, LSB valid, 即最高字节不使用
	 * bit[1] : 0 = BSWP
	 * bit[0] : 0 = HWSWP*/
	lcd_regs->lcdcon5 = S3C2410_LCDCON5_INVVLINE | 
						S3C2410_LCDCON5_INVVFRAME;
	
	//3.3分配显存(framebuffer),并把地址告诉LCD控制器
	//lcd->fix.smem_start :显存的物理地址
	//lcd->screen_base    :显存虚拟地址
	lcd->screen_base = dma_alloc_writecombine(NULL, lcd->screen_size, &lcd->fix.smem_start, GFP_KERNEL);//返回显存虚拟地址

	lcd_regs->lcdsaddr1 = (lcd->fix.smem_start>>1) & ~(3<<30);
	lcd_regs->lcdsaddr2 = ((lcd->fix.smem_start+lcd->fix.smem_len) >> 1) & 0x1fffff ;//结束地址
	lcd_regs->lcdsaddr3 = (LCD_LENGTH*BIT_WIDTH/16);//虚拟屏一行的长度(单位:半字)

	//启动lcd
	lcd_regs->lcdcon1 |= S3C2410_LCDCON1_ENVID; /* 使能LCD控制器 */
	lcd_regs->lcdcon5 |= S3C2410_LCDCON5_PWREN; /* 使能LCD本身: LCD_PWREN */
	s3c2410_gpio_setpin(S3C2410_GPB(0),1);;     /* 输出高电平, 使能背光, TQ2440的背光电路也是通过LCD_PWREN来控制的 */
	
	//4.注册
	register_framebuffer(lcd);

	return 0;
}

static void lcd_exit(void)
{
	unregister_framebuffer(lcd);
	lcd_regs->lcdcon1 &= ~(1<<0); /* 关闭LCD控制器 */
	lcd_regs->lcdcon1 &= ~(1<<3); /* 关闭LCD本身 */
	s3c2410_gpio_setpin(S3C2410_GPB(0),0);;     /* 关闭背光 */
	dma_free_writecombine(NULL, lcd->fix.smem_len, lcd->screen_base, lcd->fix.smem_start);
	iounmap(lcd_regs);
	iounmap(gpccon);
	iounmap(gpdcon);
	iounmap(gpgcon);
	framebuffer_release(lcd);
}

module_init(lcd_init);
module_exit(lcd_exit);
MODULE_LICENSE("GPL");
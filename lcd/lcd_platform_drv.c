#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <mach/regs-lcd.h>
#include <mach/regs-gpio.h>
#include <mach/fb.h>

static int __devinit s3c2410fb_probe(struct platform_device *pdev)
{
    struct fb_info *fbinfo;
    struct resource *res;
    void __iomem	*lcd_regs
    int size;

    //1.分配一个fb_info结构体
	fbinfo = framebuffer_alloc(0, NULL);
    //2.1设置固定的参数
	strcpy(fbinfo->fix.id, "mylcd");
	fbinfo->fix.smem_len = 480*272*32/8; /* TQ2440的LCD位宽是24,但是2440里会分配4字节即32位(浪费1字节) */
	fbinfo->fix.line_length = 480*32/8;
	fbinfo->fix.type     = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.visual   = FB_VISUAL_TRUECOLOR;
	
	//2.2设置可变的参数
	fbinfo->var.xres           = 480;
	fbinfo->var.yres           = 272;
	fbinfo->var.xres_virtual   = 480;
	fbinfo->var.yres_virtual   = 272;
	fbinfo->var.bits_per_pixel = 32;//每个像素用多少位
	
	fbinfo->var.red.offset     = 16;
	fbinfo->var.red.length     = 8;
	fbinfo->var.green.offset   = 8;
	fbinfo->var.green.length   = 8;
	fbinfo->var.blue.offset    = 0;
	fbinfo->var.blue.length    = 8;

	fbinfo->var.activate       = FB_ACTIVATE_NOW;
	
	//2.3设置操作函数
	fbinfo->fbops = &lcd_ops;
	
	//2.4其他
	fbinfo->pseudo_palette = pseudo_palette;//调色板
	fbinfo->screen_size = 480*272*32/8;

    //2.5分配显存(framebuffer),并把地址告诉LCD控制器
	//lcd->fix.smem_start :显存的物理地址
	//lcd->screen_base    :显存虚拟地址
	fbinfo->screen_base = dma_alloc_writecombine(NULL, fbinfo->screen_size, &fbinfo->fix.smem_start, GFP_KERNEL);//返回显存虚拟地址

    //3.1 获取io资源
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    size = resource_size(res);
    lcd_regs = ioremap(res->start, size);


    writel(fbinfo->fix.smem_start >> 1, lcd_regs + S3C2410_LCDSADDR1);
	writel(((lcd->fix.smem_start+lcd->fix.smem_len) >> 1), lcd_regs + S3C2410_LCDSADDR2);
	writel((480*32/16), lcd_regs + S3C2410_LCDSADDR3);
    //4.注册
	register_framebuffer(lcd);
}

struct platform_driver lcd_drv = {
	.probe = lcd_probe,
	.remove = lcd_remove,
	.driver		= {
		.name	= "s3c2410-lcd",
		.owner	= THIS_MODULE,
	},
};

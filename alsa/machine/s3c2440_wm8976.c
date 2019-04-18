/*参考：sound/soc/samsung/s3c24xx_uda134x.c
1.分配注册一个名为“soc-audio”的平台设备
2.给该平台设备添加一个snd_soc_card的私有数据，snd_soc_card中有一个snd_soc_dai_link,
  snd_soc_dai_link来决定ASOC的各部分驱动
*/
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <sound/soc.h>
//#include <sound/s3c24xx_uda134x.h>
#include <plat/regs-iis.h>


static void s3c24xx_wm8976_release (struct device *dev)
{

}
static struct platform_device s3c24xx_wm8976_snd_device = {
	.name = "soc-audio",
	.id = -1,
	.dev = {
		.release = s3c24xx_wm8976_release,
	}
};

static struct snd_soc_ops s3c24xx_wm8976_ops = {

};

static struct snd_soc_dai_link s3c24xx_wm8976_dai_link = {
	.name = "100ask_wm8976",
	.stream_name = "100ask_wm8976",
	.codec_name = "wm8976-codec",
	.codec_dai_name = "wm8976-iis",
	.cpu_dai_name = "s3c2440-iis",
	.ops = &s3c24xx_wm8976_ops,
	.platform_name	= "s3c2440-dma",
};

static struct snd_soc_card snd_soc_s3c24xx_wm8976 = {
	.name = "S3C24XX_WM8976",
	.owner = THIS_MODULE,
	.dai_link = &s3c24xx_wm8976_dai_link,
	.num_links = 1,
};

static int __init s3c2440_wm8976_init(void)
{
    platform_set_drvdata(&s3c24xx_wm8976_snd_device, &snd_soc_s3c24xx_wm8976);//设置私有数据
  	platform_device_register(&s3c24xx_wm8976_snd_device);
    return 0;
}

static void __exit s3c2440_wm8976_exit(void)
{
	platform_device_unregister(&s3c24xx_wm8976_snd_device);
}

module_init(s3c2440_wm8976_init);
mudule_exit(s3c2440_wm8976_exit);
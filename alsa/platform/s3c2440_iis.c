/*参考：sound/soc/samsung/s3c24xx-i2s.c
1.构造 snd_soc_dai_driver 结构体
*/
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <mach/regs-gpio.h>
#include <mach/dma.h>
#include <plat/regs-iis.h>
#include "dma.h"
#include "s3c24xx-i2s.h"

static const struct snd_soc_dai_ops s3c2440_i2s_dai_ops = {
	.trigger	= s3c24xx_i2s_trigger,
	.hw_params	= s3c24xx_i2s_hw_params,
	.set_fmt	= s3c24xx_i2s_set_fmt,
	.set_clkdiv	= s3c24xx_i2s_set_clkdiv,
	.set_sysclk	= s3c24xx_i2s_set_sysclk,
};

static struct snd_soc_dai_driver s3c24xx_i2s_dai = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C24XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = S3C24XX_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &s3c2440_i2s_dai_ops,
};

static void s3c2440_iis_dev_release (struct device *dev)
{

}
static struct platform_device s3c2440_iis_device
{
	.name = "s3c2440-iis",
	.id = -1,
	.dev = {
		.release = s3c2440_iis_dev_release,
	}
}

static int s3c2440_iis_probe(struct platform_device * pdev)
{
    return snd_soc_register_dai(&pdev->dev, &s3c24xx_i2s_dai);
}

int s3c2440_iis_remove(struct platform_device *pdev)
{
    return snd_soc_unregister_dai(&pdev->dev);
}

struct platform_driver s3c2440_iis_drv = {
	.probe = s3c2440_iis_probe,
	.remove = s3c2440_iis_remove,
	.driver = {.name = "s3c2440-iis",}//一定需要同名,才会调用probe函数
};

static int s3c2440_iis_init(void)
{
    platform_device_register(&s3c2440_iis_device);//注册平台设备
    platform_driver_register(&s3c2440_iis_drv);//注册平台驱动
    return 0;
}

static void s3c2440_iis_exit(void)
{
	platform_device_unregister(&s3c2440_iis_device);
    platform_driver_unregister(&s3c2440_iis_drv);
}

module_init(s3c2440_iis_init);
mudule_exit(s3c2440_iis_exit);
/*参考：sound/soc/samsung/dma.c
1.
*/
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/dma.h>
#include "dma.h"


static struct snd_pcm_ops s3c2440_dma_ops = {
};

static struct snd_soc_platform_driver s3c2440_dma_platform = {
	.ops		= &s3c2440_dma_ops,
};

static int s3c2440_dma_probe(struct platform_device * pdev)
{
    return snd_soc_register_platform(&pdev->dev, &s3c2440_dma_platform);
}

int s3c2440_dma_remove(struct platform_device *pdev)
{
    snd_soc_unregister_platform(&pdev->dev);
    return 0;
}

struct platform_driver s3c2440_dma_drv = {
	.probe = s3c2440_dma_probe,
	.remove = s3c2440_dma_remove,
	.driver = {.name = "s3c2440-dma",}//一定需要同名,才会调用probe函数
};


static void s3c2440_dma_dev_release (struct device *dev)
{

}
static struct platform_device s3c2440_dma_device
{
	.name = "s3c2440-dma",
	.id = -1,
	.dev = {
		.release = s3c2440_dma_dev_release,
	}
}

static int s3c2440_dma_init(void)
{
    platform_device_register(&s3c2440_dma_device);//注册平台设备
    platform_driver_register(&s3c2440_dma_drv);//注册平台驱动
    return 0;
}

static void s3c2440_dma_exit(void)
{
	platform_device_unregister(&s3c2440_dma_device);
    platform_driver_unregister(&s3c2440_dma_drv);
}

module_init(s3c2440_dma_init);
mudule_exit(s3c2440_dma_exit);
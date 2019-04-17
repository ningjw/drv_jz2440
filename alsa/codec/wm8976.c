/*参考：sound/soc/codecs/uda134x.c
1.构造 snd_soc_dai_driver 结构体
2.构造 snd_soc_codec_driver 结构体
3.注册它们
*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <sound/uda134x.h>
#include <sound/l3.h>

#include "uda134x.h"

static struct snd_soc_codec_driver soc_codec_dev_wm8976 = {

};

static const struct snd_soc_dai_ops wm8976_dai_ops = {

};
static struct snd_soc_dai_driver wm8976_dai = {
	.name = "wm8976-iis",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,//采样率
		.formats = UDA134X_FORMATS,//格式
	},
	/* capture capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = UDA134X_RATES,
		.formats = UDA134X_FORMATS,
	},
	/* pcm operations */
	.ops = &wm8976_dai_ops,
};


static void wm8976_dev_release (struct device *dev)
{

}

static int wm8976_probe(struct platform_device * pdev)
{
    return snd_soc_register_codec(&pdev->dev,&soc_codec_dev_wm8976, &wm8976_dai, 1);
}

int wm8976_remove(struct platform_device *pdev)
{
    return snd_soc_unregister_codec(&pdev->dev);
}

static struct platform_device wm8976_device
{
	.name = "wm8976-codec",
	.id = -1,
	.dev = {
		.release = wm8976_dev_release,
	}
}

struct platform_driver wm8976_drv = {
	.probe = wm8976_probe,
	.remove = wm8976_remove,
	.driver = {.name = "wm8976-codec",}//一定需要同名,才会调用probe函数
};


static int wm8976_init(void)
{
    platform_device_register(&wm8976_device);
    platform_driver_register(&wm8976_drv);
    return 0;
}

static void wm8976_exit(void)
{
	platform_device_unregister(&wm8976_device);
    platform_driver_unregister(&wm8976_drv);
}

module_init(wm8976_init);
mudule_exit(wm8976_exit);



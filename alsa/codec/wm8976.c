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
#include <asm/io.h>

#define STAT0_SC_384FS (1 << 4)
#define STAT0_DC_FILTER (1 << 0)
#define STAT1_DAC_ON (1 << 0) /* DAC powered */
#define DATA0_VOLUME(x) (x)
#define DATA1_BASS(x) ((x) << 2)
#define DATA1_TREBLE(x) ((x))
#define DATA2_DEEMP_NONE (0x0 << 3)
#define DATA2_MUTE (0x1 << 2)

/* UDA1341 registers */
#define UDA1341_DATA00 0
#define UDA1341_DATA01 1
#define UDA1341_DATA10 2
#define UDA1341_EA000  3
#define UDA1341_EA001  4
#define UDA1341_EA010  5
#define UDA1341_EA100  6
#define UDA1341_EA101  7
#define UDA1341_EA110  8
#define UDA1341_DATA1  9
#define UDA1341_STATUS0 10
#define UDA1341_STATUS1 11
#define UDA1341_REG_NUM 12

#define UDA1341_L3ADDR	5
#define UDA1341_DATA0_ADDR	((UDA1341_L3ADDR << 2) | 0)
#define UDA1341_DATA1_ADDR	((UDA1341_L3ADDR << 2) | 1)
#define UDA1341_STATUS_ADDR	((UDA1341_L3ADDR << 2) | 2)

#define UDA1341_EXTADDR_PREFIX	0xC0
#define UDA1341_EXTDATA_PREFIX	0xE0

#define UDA134X_RATES SNDRV_PCM_RATE_8000_48000
#define UDA134X_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

/* 所有寄存器的默认值 */
static const char wm8976_reg[UDA1341_REG_NUM] = {
    0x00, 0x40, 0x80,/* DATA0 */
	0x04, 0x04, 0x04, 0x00, 0x00, 0x00,/* Extended address registers */
    0x00,           /* data1 */
    0x00, 0x83,     /* status regs */
};

static const char wm8976_reg_addr[UDA1341_REG_NUM] = {
    UDA1341_DATA0_ADDR, UDA1341_DATA0_ADDR, UDA1341_DATA0_ADDR,
    0, 1, 2, 4, 5, 6,
    UDA1341_DATA1_ADDR,
    UDA1341_STATUS_ADDR, UDA1341_STATUS_ADDR
};

static const char wm8976_data_bit[UDA1341_REG_NUM] = {
    0, (1<<6), (1<<7),
    0, 0, 0, 0, 0, 0,
    0,
    0, (1<<7),
};

static volatile unsigned int *gpbdat;
static volatile unsigned int *gpbcon;

static void set_mod(int val)
{
    if (val){
        *gpbdat |= (1<<2);
    }else{
        *gpbdat &= ~(1<<2);
    }
}

static void set_clk(int val)
{
    if (val){
        *gpbdat |= (1<<4);
    }else{
        *gpbdat &= ~(1<<4);
    }
}

static void set_dat(int val)
{
    if (val){
        *gpbdat |= (1<<3);
    }else{
        *gpbdat &= ~(1<<3);
    }
}

/*
 * Send one byte of data to the chip.  Data is latched into the chip on
 * the rising edge of the clock.
 */
static void sendbyte(unsigned int byte)
{
	int i;

	for (i = 0; i < 8; i++) {
		set_clk(0);
		udelay(1);
		set_dat(byte & 1);
		udelay(1);
		set_clk(1);
		udelay(1);
		byte >>= 1;
	}
}

static void s3c2440_l3_write(u8 reg, u8 data)
{
	set_clk(1);
	set_dat(1);
	set_mod(1);
	udelay(1);

	set_mod(0);
	udelay(1);
	sendbyte(reg);
	udelay(1);

	set_mod(1);
	sendbyte(data);

	set_clk(1);
	set_dat(1);
	set_mod(0);
}


/*
 * The codec has no support for reading its registers except for peak level...
 */
static inline unsigned int wm8976_read_reg_cache(struct snd_soc_codec *codec,unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if (reg >= UDA1341_REG_NUM)
		return -1;
	return cache[reg];
}
/*
 * Write to the uda134x registers
 *
 */
static int wm8976_write_reg(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	u8 *cache = codec->reg_cache;
	if(reg >= UDA1341_REG_NUM){
		return -1;
	}
	if(reg >= UDA1341_EA000 && reg <= UDA1341_EA110){
		s3c2440_l3_write(UDA1341_DATA0_ADDR,wm8976_reg_addr[reg] | UDA1341_EXTADDR_PREFIX);
		s3c2440_l3_write(UDA1341_DATA0_ADDR,value | UDA1341_EXTDATA_PREFIX);
	}else{
		s3c2440_l3_write(wm8976_reg_addr[reg], value| wm8976_data_bit[reg]);
	}

	return 0;
}

void wm8976_init_regs(struct snd_soc_codec *codec)
{
	*gpbcon &= ~((3<<4)|(3<<6)|(3<<8));
	*gpbcon |= (1<<4)|(1<<6)|(1<<8);

	wm8976_write_reg(codec, UDA1341_STATUS0, 0x40 | STAT0_SC_384FS | STAT0_DC_FILTER);// reset uda1341
	wm8976_write_reg(codec, UDA1341_STATUS1, STAT1_DAC_ON);//启动DAC
	wm8976_write_reg(codec, UDA1341_DATA00, DATA0_VOLUME(0x0) );// maximum volume
	wm8976_write_reg(codec, UDA1341_DATA01, DATA1_BASS(0)| DATA1_TREBLE(0) );
	wm8976_write_reg(codec, UDA1341_DATA10, DATA2_DEEMP_NONE &~(DATA2_MUTE) );
}

static int wm8976_soc_probe(struct snd_soc_codec *codec)
{
	wm8976_init_regs(codec);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8976 = {
	.probe =        wm8976_soc_probe,
	//寄存器不支持读操作，所以用一段缓存保存寄存器值，每次写寄存器的同时写该缓存。
	.reg_cache_size = sizeof(wm8976_reg),//保存寄存器的缓存有多大
	.reg_word_size = sizeof(u8),    //每个寄存器长度
	.reg_cache_default = wm8976_reg,//默认值保存在哪里
	.reg_cache_step = 1,
	.read = wm8976_read_reg_cache,//读寄存器
	.write = wm8976_write_reg,        //写寄存器
};

static int wm8976_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *params,
							 struct snd_soc_dai *dai)
{
	//根据para设置wm8976的寄存器

	return 0;
}

static const struct snd_soc_dai_ops wm8976_dai_ops = {
	.hw_params	= wm8976_hw_params,
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
    snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_device wm8976_device = {
	.name = "wm8976-codec",
	.id = -1,
	.dev = {
		.release = wm8976_dev_release,
	},
};

struct platform_driver wm8976_drv = {
	.probe = wm8976_probe,
	.remove = wm8976_remove,
	.driver = {
		.name = "wm8976-codec",//一定需要同名,才会调用probe函数
	},
};


static int wm8976_init(void)
{
	gpbcon = ioremap(0x56000010,4);
	gpbdat = ioremap(0x56000014,4);
    platform_device_register(&wm8976_device);
    platform_driver_register(&wm8976_drv);
    return 0;
}

static void wm8976_exit(void)
{
	iounmap(gpbcon);
	iounmap(gpbdat);
	platform_device_unregister(&wm8976_device);
    platform_driver_unregister(&wm8976_drv);
}

module_init(wm8976_init);
mudule_exit(wm8976_exit);



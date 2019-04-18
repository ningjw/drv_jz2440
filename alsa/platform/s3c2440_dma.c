/*参考：sound/soc/samsung/dma.c
1.分配DMA BUFFER
2.从BUFFER里取出period
3.启动DMA传输
4.传输完毕，更新状态
*/
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/dma.h>

struct s3c_dma_regs {
	unsigned long disrc;
	unsigned long disrcc;
	unsigned long didst;
	unsigned long didstc;
	unsigned long dcon;
	unsigned long dstat;
	unsigned long dcsrc;
	unsigned long dcdst;
	unsigned long dmasktrig;
};
static volatile struct s3c_dma_regs *dma_regs;

static const struct snd_pcm_hardware s3c2440_dma_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				    SNDRV_PCM_INFO_BLOCK_TRANSFER |
				    SNDRV_PCM_INFO_MMAP |
				    SNDRV_PCM_INFO_MMAP_VALID |
				    SNDRV_PCM_INFO_PAUSE |
				    SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_U16_LE |
				    SNDRV_PCM_FMTBIT_U8 |
				    SNDRV_PCM_FMTBIT_S8,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= PAGE_SIZE*2,
	.periods_min		= 2,
	.periods_max		= 128,
	.fifo_size		= 32,
};

struct s3c2440_dma_info{
	unsigned int buf_max_size;
	unsigned int period_size;
	unsigned int buffer_size;
	unsigned int phy_addr;
	unsigned int virt_addr;
	unsigned int addr_ofs;
	unsigned int be_running;
};
static struct s3c2440_dma_info dma_info;


/* 数据传输: 源,目的,长度 */
static void load_dma_period(void)
{
	/* 把源,目的,长度告诉DMA */
	dma_regs->disrc      = dma_info.phy_addr + dma_info.addr_ofs;/* 源的物理地址 */
	dma_regs->disrcc     = (0<<1) | (0<<0); /* 源位于AHB总线, 源地址递增 */
	dma_regs->didst      = 0x55000010;        /* 目的的物理地址 */
	dma_regs->didstc     = (0<<2) | (1<<1) | (1<<0); /* 目的位于APB总线, 目的地址不变 */
	dma_regs->dcon       = (1<<31)|(0<<30)|(1<<29)|(0<<28)|(0<<27)|(0<<24)|(1<<23)|(1<<20)|(dma_info.period_size/2);  /* 使能中断,单个传输,硬件触发 */
}

static void s3c2440_dma_start(void)
{
	dma_regs->dmasktrig  = (1<<1);/* 启动DMA */
}

static void s3c2440_dma_stop(void)
{
	dma_regs->dmasktrig  &= ~(1<<1);/* 停止DMA */
}


static irqreturn_t irq_handler_dma2(int irq, void *dev_id)
{
	struct snd_pcm_substream *substream = dev_id;

	//更新状态信息
	dma_info.addr_ofs += dma_info.period_size;
	if(dma_info.addr_ofs >= dma_info.buffer_size){
		dma_info.addr_ofs = 0;
	}
	//如果buffer里没有数据了，则调用triger函数停止DMA
	snd_pcm_period_elapsed(substream);
	if(dma_info.be_running){
		load_dma_period();
		s3c2440_dma_start();
	}
	//如果还有数据：加载下一个period，再次触发DMA
	return IRQ_HANDLED;
}

static int s3c2440_dma_open(struct snd_pcm_substream *substream)
{
	int ret;
	struct snd_pcm_runtime *runtime = substream->runtime;
	//设置属性
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &s3c2440_dma_hardware);
	//申请中断
	request_irq(IRQ_DMA2,irq_handler_dma2,IRQF_DISABLED,"alsa for play",substream);
	if(ret){
		printk("request_irq err\n");
		return -EIO;
	}
	return 0;
}

static int s3c2440_dma_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long total_bytes = params_buffer_bytes(params);
	//根据params设置DMA相关寄存器
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = total_bytes;
	dma_info.buffer_size = total_bytes;
	dma_info.period_size = params_period_bytes(params);
	return 0;
}

static int s3c2440_dma_prepare(struct snd_pcm_substream *substream)
{
	//准备DMA传输
	
	//复位各状态
	dma_info.addr_ofs = 0;
	dma_info.be_running = 0;
	//加载第一个period
	load_dma_period();
	return 0;
}

static int s3c2440_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	//根据cmd启动或者停止DMA传输
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		//启动DMA传输
		s3c2440_dma_start();
		dma_info.be_running = 1;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		//停止DMA传输
		s3c2440_dma_stop();
		dma_info.be_running = 0;
		break;

	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static snd_pcm_uframes_t s3c2440_dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long res = dma_info.addr_ofs;
	if (res >= snd_pcm_lib_buffer_bytes(substream)) {
		if (res == snd_pcm_lib_buffer_bytes(substream))
			res = 0;
	}
	return bytes_to_frames(substream->runtime, res);
}

static int s3c2440_dma_close(struct snd_pcm_substream *substream)
{
	free_irq(IRQ_DMA2,substream);
	return 0;
}

static struct snd_pcm_ops s3c2440_dma_ops = {
	.open		= s3c2440_dma_open,
	.hw_params	= s3c2440_dma_hw_params,
	.prepare	= s3c2440_dma_prepare,
	.trigger	= s3c2440_dma_trigger,
	.pointer	= s3c2440_dma_pointer,
	.close		= s3c2440_dma_close,
};

static int s3c2440_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	//分配DMA buffer
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		dma_info.virt_addr = dma_alloc_writecombine(pcm->card->dev, 
		                                 s3c2440_dma_hardware.buffer_bytes_max,
					                     &dma_info.phy_addr, GFP_KERNEL);
		if(!dma_info.virt_addr){
			return -ENOMEM;
		}
		dma_info.buf_max_size = s3c2440_dma_hardware.buffer_bytes_max;
	}

	return 0;
}

static void s3c2440_dma_free(struct snd_pcm *pcm)
{
	//释放DMA buffer
	dma_free_writecombine(pcm->card->dev, dma_info.buf_max_size,
						 dma_info.virt_addr, dma_info.phy_addr);
}

static struct snd_soc_platform_driver s3c2440_dma_platform = {
	.ops		= &s3c2440_dma_ops,
	.pcm_new	= s3c2440_dma_new,
	.pcm_free	= s3c2440_dma_free,
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
static struct platform_device s3c2440_dma_device = {
	.name = "s3c2440-dma",
	.id = -1,
	.dev = {
		.release = s3c2440_dma_dev_release,
	}
};

static int s3c2440_dma_init(void)
{
	dma_regs = ioremap(0x4B000080, sizeof(struct s3c_dma_regs));
    platform_device_register(&s3c2440_dma_device);//注册平台设备
    platform_driver_register(&s3c2440_dma_drv);//注册平台驱动
    return 0;
}

static void s3c2440_dma_exit(void)
{
	iounmap(dma_regs);
	platform_device_unregister(&s3c2440_dma_device);
    platform_driver_unregister(&s3c2440_dma_drv);
}

module_init(s3c2440_dma_init);
mudule_exit(s3c2440_dma_exit);
//参考:drivers\mtd\nand\s3c2410.c	
//    drivers\mtd\nand\at91_nand.c
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

static struct nand_chip *nand;
static struct mtd_info *mtd;
struct nand_regs{
	unsigned long nfconf;  
	unsigned long nfcont;
	unsigned long nfcmd;
	unsigned long nfaddr;
	unsigned long nfdata;
	unsigned long nfmecc0;
	unsigned long nfmecc1;
	unsigned long nfsecc;
	unsigned long nfstat;
	unsigned long nfestat0;
	unsigned long nfestat1;
	unsigned long nfmecc0s;
	unsigned long nfmecc1s;
	unsigned long nfseccs;
	unsigned long nfsblk;
	unsigned long nfeblk;
};
static struct nand_regs *nand_regs;
//分区信息可从arch/arm/mach-s3c24xx/common-smdk.c文件中获得
static struct mtd_partition nand_part[] = {
	[0] = {
		.name	= "bootloader",
		.size	= 0x00040000,
		.offset	= 0,
	},
	[1] = {
		.name	= "params",
		.offset = MTDPART_OFS_APPEND,
		.size	= 0x00020000,
	},
	[2] = {
		.name	= "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size	= 0x00400000,
	},
	[3] = {
		.name	= "rootfs",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	}
};
	

static void s3c2440_select_chip(struct mtd_info *mtd, int chip)
{
	if(chip == -1){//取消片选  NFCONT[1]设为1
		nand_regs->nfcont |= (1<<1);		
	}else{//选中 NFCONT[1]设为0
		nand_regs->nfcont &= ~(1<<1);
	}
}

static void	s3c2440_cmd_ctrl(struct mtd_info *mtd, int dat,unsigned int ctrl)
{
	if(ctrl & NAND_CLE){//发送命令
		nand_regs->nfcmd = dat;
	}else{//发送地址
		nand_regs->nfaddr = dat;
	}
}

static int	s3c2440_dev_ready(struct mtd_info *mtd)
{
	return (nand_regs->nfstat & (1<<0));//NFSTAT的bit[0]
}


static int nand_init(void)
{
	struct clk *clk;
	//1.分配一个nand_chip结构体
	nand = kzalloc(sizeof(struct nand_chip), GFP_KERNEL);
	nand_regs = ioremap(0x4E000000, sizeof(struct nand_regs));
	
	//2.设置
	nand->select_chip = s3c2440_select_chip;//
	nand->cmd_ctrl = s3c2440_cmd_ctrl;      //
	nand->IO_ADDR_R = &nand_regs->nfdata;//NFDATA的虚拟地址
	nand->IO_ADDR_W = &nand_regs->nfdata;//NFDATA的虚拟地址
	nand->dev_ready = s3c2440_dev_ready; //
	nand->ecc.mode = NAND_ECC_SOFT;      //使用软件ECC校验
	
	//3.硬件相关:根据手册设置时间参数
	/*使能NAND Flash时钟*/
	clk = clk_get(NULL, "nand");
	clk_enable(clk);
	
	/*HCLK = 100MHz
	TACLS : 发出CLE/ALE之后, 多长时间发出nWE信号. 从手册可知两个信号可以同时发出,所以TACLS = 0
	TWRPH0: 通过nWE脉冲宽度计算得出; HCLK x ( TWRPH0 + 1 ) >= 12ns ,计算得出TWRPH0>=1
	TWRPH1: nWE信号变为高电平后,CLE/ALE多长时间变为低电平;HCLK x ( TWRPH1 + 1 ) >= 5ns,得出TWRPH1>=0
	*/
#define TACLS  0
#define TWRPH0 1
#define TWRPH1 0
	nand_regs->nfconf =(TACLS<<12) | (TWRPH0<<8) | (TWRPH1<<4);

	/*NFCONT:
	bit1 = 1//Force nFCE to high (Disable chip select)
	bit0 = 1//NAND flash controller enable*/
	nand_regs->nfcont = (1<<1) | (1<<0);
	
	//4.使用:nand_scan
	mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	mtd->owner = THIS_MODULE;
	mtd->priv = nand;
	nand_scan(mtd, 1);
	//5.add_mtd_partitions
    mtd_device_parse_register(mtd,NULL, NULL,nand_part, 4);

	return 0;
}

static void nand_exit(void)
{
	nand_release(mtd);
	kfree(mtd);
	kfree(nand);
	iounmap(nand_regs);
}

module_init(nand_init);
module_exit(nand_exit);
MODULE_LICENSE("GPL");
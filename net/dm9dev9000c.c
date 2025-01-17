#define DRV_NAME	"dm9KS"
#define DRV_VERSION	"2.09"
#define DRV_RELDATE	"2007-11-22"

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/init.h>				
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <asm/dma.h>
#include <linux/spinlock.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "dm9000.h"


/* Board/System/Debug information/definition ---------------- */

#define DM9KS_ID		0x90000A46
#define DM9010_ID		0x90100A46
/*-------register name-----------------------*/
#define DM9KS_NCR	 	  0x00	/* Network control Reg.*/
#define DM9KS_NSR		  0x01	/* Network Status Reg.*/
#define DM9KS_TCR		  0x02	/* TX control Reg.*/
#define DM9KS_RXCR		0x05	/* RX control Reg.*/
#define DM9KS_BPTR		0x08
#define DM9KS_FCTR		0x09
#define DM9KS_FCR			0x0a
#define DM9KS_EPCR		0x0b
#define DM9KS_EPAR		0x0c
#define DM9KS_EPDRL		0x0d
#define DM9KS_EPDRH		0x0e
#define DM9KS_GPR			0x1f	/* General purpose register */
#define DM9KS_CHIPR		0x2c
#define DM9KS_TCR2		0x2d
#define DM9KS_SMCR		0x2f 	/* Special Mode Control Reg.*/
#define DM9KS_ETXCSR	0x30	/* Early Transmit control/status Reg.*/
#define	DM9KS_TCCR		0x31	/* Checksum cntrol Reg. */
#define DM9KS_RCSR		0x32	/* Receive Checksum status Reg.*/
#define DM9KS_BUSCR		0x38
#define DM9KS_MRCMDX	0xf0
#define DM9KS_MRCMD		0xf2
#define DM9KS_MDRAL		0xf4
#define DM9KS_MDRAH		0xf5
#define DM9KS_MWCMD		0xf8
#define DM9KS_MDWAL		0xfa
#define DM9KS_MDWAH		0xfb
#define DM9KS_TXPLL		0xfc
#define DM9KS_TXPLH		0xfd
#define DM9KS_ISR			0xfe
#define DM9KS_IMR			0xff
/*---------------------------------------------*/
#define DM9KS_REG05		0x30	/* SKIP_CRC/SKIP_LONG */ 
#define DM9KS_REGFF		0xA3	/* IMR */
#define DM9KS_DISINTR	0x80

#define DM9KS_PHY			0x40	/* PHY address 0x01 */
#define DM9KS_PKT_RDY		0x01	/* Packet ready to receive */

#define DM9KS_MIN_IO		0x300
#define DM9KS_MAX_IO		0x370
#define DM9KS_IRQ		    3

#define DM9KS_VID_L		0x28
#define DM9KS_VID_H		0x29
#define DM9KS_PID_L		0x2A
#define DM9KS_PID_H		0x2B

#define DM9KS_RX_INTR		0x01
#define DM9KS_TX_INTR		0x02
#define DM9KS_LINK_INTR		0x20

#define DM9KS_DWORD_MODE	1
#define DM9KS_BYTE_MODE		2
#define DM9KS_WORD_MODE		0

#define TRUE			1
#define FALSE			0
/* Number of continuous Rx packets */
#define CONT_RX_PKT_CNT		0xFFFF

#define DMFE_TIMER_WUT  jiffies+(HZ*5)	/* timer wakeup time : 5 second */

#ifdef DM9KS_DEBUG
#define DMFE_DBUG(dbug_now, msg, vaule)\
if (dmfe_debug||dbug_now) printk(KERN_ERR "dmfe: %s %x\n", msg, vaule)
#else
#define DMFE_DBUG(dbug_now, msg, vaule)\
if (dbug_now) printk(KERN_ERR "dmfe: %s %x\n", msg, vaule)
#endif

#pragma pack(push, 1)
typedef struct _RX_DESC
{
	u8 rxbyte;
	u8 status;
	u16 length;
}RX_DESC;

typedef union{
	u8 buf[4];
	RX_DESC desc;
} rx_t;
#pragma pack(pop)

enum DM9KS_PHY_mode {
	DM9KS_10MHD   = 0, //10M中速模式
	DM9KS_100MHD  = 1, //100M中速模式
	DM9KS_10MFD   = 4, //10M高速模式
	DM9KS_100MFD  = 5, //100M高速模式
	DM9KS_AUTO    = 8, //自适配模式
};

/* Structure/enum declaration ------------------------------- */
typedef struct board_info { 
	u32 io_addr;/* Register I/O base address */
	u32 io_data;/* Data I/O address */
	u8 op_mode;/* PHY operation mode */
	u8 io_mode;/* 0:word, 2:byte */
	u8 Speed;	/* current speed */
	u8 chip_revision;
	int rx_csum;/* 0:disable, 1:enable */
	
	u32 reset_counter;/* counter: RESET */ 
	u32 reset_tx_timeout;/* RESET caused by TX Timeout */
	int tx_pkt_cnt;
	int cont_rx_pkt_cnt;/* current number of continuos rx packets  */
	struct net_device_stats stats;
	
	struct timer_list timer;
	unsigned char srom[128];
	spinlock_t lock;
	struct mii_if_info mii;
} board_info_t;
/* Global variable declaration ----------------------------- */
/*static int dmfe_debug = 0;*/
static struct net_device * dmfe_dev = NULL;
static struct ethtool_ops dmfe_ethtool_ops;
/* For module input parameter */
static int mode       = DM9KS_AUTO;  
static int media_mode = DM9KS_AUTO;
static int  irq        = DM9KS_IRQ;
static int iobase     = DM9KS_MIN_IO;

/* function declaration ------------------------------------- */
int dmfe_probe1(struct net_device *);
static int dmfe_open(struct net_device *);
static int dmfe_start_xmit(struct sk_buff *, struct net_device *);
static void dmfe_tx_done(unsigned long);
static void dmfe_packet_receive(struct net_device *);
static int dmfe_stop(struct net_device *);
static struct net_device_stats * dmfe_get_stats(struct net_device *); 
static int dmfe_do_ioctl(struct net_device *, struct ifreq *, int);
static irqreturn_t dmfe_interrupt(int irq, void *dev_id);/* for kernel 2.6.20 */
static void dmfe_timer(unsigned long);
static void dmfe_init_dm9000(struct net_device *);
u8 ior(board_info_t *, int);
void iow(board_info_t *, int, u8);
static u16 phy_read(board_info_t *, int);
static void phy_write(board_info_t *, int, u16);
static u16 read_srom_word(board_info_t *, int);
static void dm9000_hash_table(struct net_device *);
static void dmfe_timeout(struct net_device *);
static void dmfe_reset(struct net_device *);
static int mdio_read(struct net_device *, int, int);
static void mdio_write(struct net_device *, int, int, int);
static void dmfe_get_drvinfo(struct net_device *, struct ethtool_drvinfo *);
static int dmfe_get_settings(struct net_device *, struct ethtool_cmd *);
static int dmfe_set_settings(struct net_device *, struct ethtool_cmd *);
static u32 dmfe_get_link(struct net_device *);
static int dmfe_nway_reset(struct net_device *);

static const struct net_device_ops dm9k_netdev_ops = {
	.ndo_open		= dmfe_open,
	.ndo_stop		= dmfe_stop,
	.ndo_start_xmit		= dmfe_start_xmit,//发送包函数
	.ndo_tx_timeout		= dmfe_timeout,//当watchdog超时时调用该函数。
	.ndo_set_rx_mode	= dm9000_hash_table,//设置DM9000的组播地址
	.ndo_do_ioctl		= dmfe_do_ioctl,//dm9000的ioctl实际上是使用了mii的ioctl
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_get_stats      = dmfe_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= dm9000_poll_controller,
#endif
};

/* DM9000 network baord routine ---------------------------- */

/*
  Search DM9000 board, allocate space and register it
*/
struct net_device * __init dmfe_probe(void)
{
	struct net_device *dev;
	int err;
	
	DMFE_DBUG(0, "dmfe_probe()",0);

	dev= alloc_etherdev(sizeof(struct board_info));//分配net_device结构体
	if(!dev)
		return ERR_PTR(-ENOMEM);

	err = dmfe_probe1(dev);//设置net_device结构体
	if (err)
		goto out;

	err = register_netdev(dev);//注册net_device结构体
	if (err)
		goto out1;

	return dev;
out1:
	release_region(dev->base_addr,2);
out:
	free_netdev(dev);

	return ERR_PTR(err);
}



int __init dmfe_probe1(struct net_device *dev)
{
	struct board_info *db;    /* Point a board information structure */
	u32 id_val;
	u16 i, dm9000_found = FALSE;
	u8 MAC_addr[6]={0x00,0x60,0x6E,0x33,0x44,0x55};
	u8 HasEEPROM=0,chip_info;
	DMFE_DBUG(0, "dmfe_probe1()",0);

	/* Search All DM9000 serial NIC */
	do {
		//发送命令读取VID与PID，保存到id_val变量
		outb(DM9KS_VID_L, iobase); /* DM9000C的索引寄存器(cmd引脚为0) */
		id_val = inb(iobase + 4);  /* 读DM9000C的数据寄存器(cmd引脚为1) */
		outb(DM9KS_VID_H, iobase);
		id_val |= inb(iobase + 4) << 8;
		outb(DM9KS_PID_L, iobase);
		id_val |= inb(iobase + 4) << 16;
		outb(DM9KS_PID_H, iobase);
		id_val |= inb(iobase + 4) << 24;
    
		if (id_val == DM9KS_ID || id_val == DM9010_ID) {//id_val = 0x90000A46
			
			/* Request IO from system 
			申请一块输入输出区域。 如果这段I/O端口没有被占用，在我们的驱动程序中就可以使用它。
			在使用之前，必须向系统登记，以防止被其他程序占用。登记后，在/proc/ioports文件中可以看到你登记的io口。
			在对I/O口登记后，就可以放心地用inb()， outb()之类的函来访问了
			目的在于实现资源的互斥访问*/ 
			if(!request_region(iobase, 2, dev->name))
				return -ENODEV;

			printk(KERN_ERR"<DM9KS> I/O: %x, VID: %x \n",iobase, id_val);
			dm9000_found = TRUE;

			/* Allocated board information structure */
			db = netdev_priv(dev);//分配一个board_info结构体
			memset(db, 0, sizeof(struct board_info));//初始化为0
			dmfe_dev    = dev;//net_device结构体保存为全局变量
			db->io_addr  = iobase;//设置地址寄存器
			db->io_data = iobase + 4;//设置数据寄存器
			db->chip_revision = ior(db, DM9KS_CHIPR);//获取芯片版本信息
			
			chip_info = ior(db,0x43);//貌似可以注释掉

			//设置net_device_ops结构体，该结构体包含了各种网络相关函数指针，例如收发数据
			dev->netdev_ops	= &dm9k_netdev_ops;
			dev->base_addr 	= iobase;//设置地址寄存器
			dev->irq 		= irq;//设置中断
			dev->watchdog_timeo	= 5*HZ;	//设置看门狗超时时间为5*1000

      //设置ethtool_ops结构体，通过该结构体可以获取与设置网络参数
			dev->ethtool_ops = &dmfe_ethtool_ops;

#ifdef CHECKSUM
			dev->features |=  NETIF_F_IP_CSUM|NETIF_F_SG;
#endif
			//设置mii接口
			db->mii.dev = dev;//将net_device结构体赋值给board_info结构体下的mii成员变量
			db->mii.mdio_read = mdio_read;//MII Read a word 函数
			db->mii.mdio_write = mdio_write;//MII Write a word 函数
			db->mii.phy_id = 1;
			db->mii.phy_id_mask = 0x1F; //这个给驱动用，dm9000没用，是考虑smi总线上有多个phy时。
			db->mii.reg_num_mask = 0x1F; //寄存器mask

			for (i=0; i<64; i++)
				((u16 *)db->srom)[i] = read_srom_word(db, i);//Read a word data from SROM

			/* Get the PID and VID from EEPROM to check 检测DM9000是否挂接了EEPROM
			如果你的DM9000没有挂EEPROM的话,每次上电MAC都是随机的,你需要在初始化过程里面向PAR寄存器写入你希望的MAC 
			如果挂了EEPROM,则通过指令改写EEPROM中对应的值,这样上电就是你新设置的值了.*/
			id_val = (((u16 *)db->srom)[4])|(((u16 *)db->srom)[5]<<16); 
			printk("id_val=%x\n", id_val);
			if (id_val == DM9KS_ID || id_val == DM9010_ID) 
				HasEEPROM =1;
			
			/* Set Node Address 设置MAC地址*/
			for (i=0; i<6; i++)
			{
				if (HasEEPROM) /* use EEPROM */
					dev->dev_addr[i] = db->srom[i];
				else	/* No EEPROM */
					dev->dev_addr[i] = MAC_addr[i];
			}
		}
		iobase += 0x10;
	}while(!dm9000_found && iobase <= DM9KS_MAX_IO);

	return dm9000_found ? 0:-ENODEV;
}


/*
  Open the interface.
  The interface is opened whenever "ifconfig" actives it.
*/
static int dmfe_open(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);//获取board_info_t结构体
	u8 reg_nsr;
	int i;
	DMFE_DBUG(0, "dmfe_open", 0);

	/* 注册中断 上升沿中断*/
	if (request_irq(dev->irq,&dmfe_interrupt, IRQF_TRIGGER_RISING,dev->name,dev)) 
		return -EAGAIN;

	/* 初始化网卡 */
	dmfe_init_dm9000(dev);

	/* Init driver variable */
	db->reset_counter 	= 0;//初始化计数器
	db->reset_tx_timeout 	= 0;//初始化发送超时
	db->cont_rx_pkt_cnt	= 0;//初始化接受到的数据包个数
	
	/* check link state and media speed 检测连接状态和网络速度*/
	db->Speed =10;//先假设速度为10M
	i=0;
	do {
		reg_nsr = ior(db,DM9KS_NSR);//读取链接状态
		if(reg_nsr & 0x40) /* bit6: 0 = link failed ； 1 = link ok*/
		{
			/* wait for detected Speed */
			mdelay(200);
			reg_nsr = ior(db,DM9KS_NSR);//读网络速度 
			if(reg_nsr & 0x80)//bit7 = 1 = 10M； bit7 = 0 = 100M
				db->Speed =10;
			else
				db->Speed =100;
			break;
		}
		i++;
		mdelay(1);
	}while(i<3000);	/* wait 3 second  */
	//printk("i=%d  Speed=%d\n",i,db->Speed);	
	/* set and active a timer process */
	init_timer(&db->timer);//初始化定时器
	db->timer.expires 	= DMFE_TIMER_WUT;//5秒后产生定时中断
	db->timer.data 		= (unsigned long)dev;
	db->timer.function 	= &dmfe_timer;
	add_timer(&db->timer);	//添加定时器
 	
	netif_start_queue(dev);//

	return 0;
}

/* Set PHY operationg mode
*/
static void set_PHY_mode(board_info_t *db)
{
	/* Fiber mode */
	phy_write(db, DM9000_PAR, 0x4014);
	phy_write(db, 0, 0x2100);

	if (db->chip_revision == 0x1A)//1A = DM9000CEP
	{
		//set 10M TX idle =65mA (TX 100% utility is 160mA)
		phy_write(db,20, phy_read(db,20)|(1<<11)|(1<<10));
		
		//:fix harmonic
		//For short code:
		//PHY_REG 27 (1Bh) <- 0000h
		phy_write(db, 27, 0x0000);
		//PHY_REG 27 (1Bh) <- AA00h
		phy_write(db, 27, 0xaa00);

		//PHY_REG 27 (1Bh) <- 0017h
		phy_write(db, 27, 0x0017);
		//PHY_REG 27 (1Bh) <- AA17h
		phy_write(db, 27, 0xaa17);

		//PHY_REG 27 (1Bh) <- 002Fh
		phy_write(db, 27, 0x002f);
		//PHY_REG 27 (1Bh) <- AA2Fh
		phy_write(db, 27, 0xaa2f);
		
		//PHY_REG 27 (1Bh) <- 0037h
		phy_write(db, 27, 0x0037);
		//PHY_REG 27 (1Bh) <- AA37h
		phy_write(db, 27, 0xaa37);
		
		//PHY_REG 27 (1Bh) <- 0040h
		phy_write(db, 27, 0x0040);
		//PHY_REG 27 (1Bh) <- AA40h
		phy_write(db, 27, 0xaa40);
		
		//For long code:
		//PHY_REG 27 (1Bh) <- 0050h
		phy_write(db, 27, 0x0050);
		//PHY_REG 27 (1Bh) <- AA50h
		phy_write(db, 27, 0xaa50);
		
		//PHY_REG 27 (1Bh) <- 006Bh
		phy_write(db, 27, 0x006b);
		//PHY_REG 27 (1Bh) <- AA6Bh
		phy_write(db, 27, 0xaa6b);
		
		//PHY_REG 27 (1Bh) <- 007Dh
		phy_write(db, 27, 0x007d);
		//PHY_REG 27 (1Bh) <- AA7Dh
		phy_write(db, 27, 0xaa7d);
		
		//PHY_REG 27 (1Bh) <- 008Dh
		phy_write(db, 27, 0x008d);
		//PHY_REG 27 (1Bh) <- AA8Dh
		phy_write(db, 27, 0xaa8d);
		
		//PHY_REG 27 (1Bh) <- 009Ch
		phy_write(db, 27, 0x009c);
		//PHY_REG 27 (1Bh) <- AA9Ch
		phy_write(db, 27, 0xaa9c);
		
		//PHY_REG 27 (1Bh) <- 00A3h
		phy_write(db, 27, 0x00a3);
		//PHY_REG 27 (1Bh) <- AAA3h
		phy_write(db, 27, 0xaaa3);
		
		//PHY_REG 27 (1Bh) <- 00B1h
		phy_write(db, 27, 0x00b1);
		//PHY_REG 27 (1Bh) <- AAB1h
		phy_write(db, 27, 0xaab1);
		
		//PHY_REG 27 (1Bh) <- 00C0h
		phy_write(db, 27, 0x00c0);
		//PHY_REG 27 (1Bh) <- AAC0h
		phy_write(db, 27, 0xaac0);
		
		//PHY_REG 27 (1Bh) <- 00D2h
		phy_write(db, 27, 0x00d2);
		//PHY_REG 27 (1Bh) <- AAD2h
		phy_write(db, 27, 0xaad2);
		
		//PHY_REG 27 (1Bh) <- 00E0h
		phy_write(db, 27, 0x00e0);
		//PHY_REG 27 (1Bh) <- AAE0h
		phy_write(db, 27, 0xaae0);
		//PHY_REG 27 (1Bh) <- 0000h
		phy_write(db, 27, 0x0000);
	}
}

/* 
	Initilize dm9000 board
*/
static void dmfe_init_dm9000(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);//获取net_device私有数据
	DMFE_DBUG(0, "dmfe_init_dm9000()", 0);

	spin_lock_init(&db->lock);//初始化自旋锁
	
	iow(db, DM9KS_GPR, 0);	/* GPR (reg_1Fh)bit GPIO0=0 ； PHYPD = 0 =PHY power up PHY*/
	mdelay(20);		          /* wait for PHY power-on ready */

	/* do a software reset and wait 20us */
	//3 = 011 
	//LBK = 01 = MAC Internal look-back，RST = 1 = Software reset and auto clear after 10us
	iow(db, DM9KS_NCR, 3);
	udelay(20);		/* wait 20us at least for software reset ok */
	iow(db, DM9KS_NCR, 3);	/* NCR (reg_00h) bit[0] RST=1 & Loopback=1, reset on */
	udelay(20);		/* wait 20us at least for software reset ok */

	/* 读I/O mode ，0 = 16bit；1=8bit*/
	db->io_mode = ior(db, DM9KS_ISR) >> 6; /* ISR bit7:6 keeps I/O mode */

	/* Set PHY */
	db->op_mode = media_mode;
	set_PHY_mode(db);

	/* Program operating register */
	iow(db, DM9KS_NCR, 0);    //支持内部PHY（网络控制寄存器）
	iow(db, DM9KS_TCR, 0);		/* TX Polling clear */
	iow(db, DM9KS_BPTR, 0x3f);	/* Less 3kb, 600us */
	iow(db, DM9KS_SMCR, 0);		/* Special Mode */
	iow(db, DM9KS_NSR, 0x2c);	/* clear TX status */
	iow(db, DM9KS_ISR, 0x0f); 	/* Clear interrupt status */
	iow(db, DM9KS_TCR2, 0x80);	/* Set LED mode 1 */
	if (db->chip_revision == 0x1A){ //1A = DM9000CEP
		/* Data bus current driving/sinking capability  */
		iow(db, DM9KS_BUSCR, 0x01);	/* default: 2mA */
	}
#ifdef FLOW_CONTROL
	iow(db, DM9KS_BPTR, 0x37);
	iow(db, DM9KS_FCTR, 0x38);
	iow(db, DM9KS_FCR, 0x29);
#endif

	if (dev->features & NETIF_F_HW_CSUM){
		printk(KERN_INFO "DM9KS:enable TX checksum\n");
		iow(db, DM9KS_TCCR, 0x07);	/* TX UDP/TCP/IP checksum enable */
	}
	if (db->rx_csum){
		printk(KERN_INFO "DM9KS:enable RX checksum\n");
		iow(db, DM9KS_RCSR, 0x02);	/* RX checksum enable */
	}

#ifdef ETRANS
	/*If TX loading is heavy, the driver can try to anbel "early transmit".
	The programmer can tune the "Early Transmit Threshold" to get 
	the optimization. (DM9KS_ETXCSR.[1-0])
	
	Side Effect: It will happen "Transmit under-run". When TX under-run
	always happens, the programmer can increase the value of "Early 
	Transmit Threshold". */
	iow(db, DM9KS_ETXCSR, 0x83);
#endif
 
	/* Set address filter table */
	dm9000_hash_table(dev);

	/* Activate DM9000/DM9010 */
	iow(db, DM9KS_IMR, DM9KS_REGFF); /* Enable TX/RX interrupt mask */
	iow(db, DM9KS_RXCR, DM9KS_REG05 | 1);	/* RX enable */
	
	/* Init Driver variable */
	db->tx_pkt_cnt 		= 0;
		
	netif_carrier_on(dev);

}

/*
  Hardware start transmission.
  Send a packet to media from the upper layer.
*/
static int dmfe_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	char * data_ptr;
	int i, tmplen;
	u16 MDWAH, MDWAL;
	
	#ifdef TDBUG /* check TX FIFO pointer */
			u16 MDWAH1, MDWAL1;
			u16 tx_ptr;
	#endif
	
	DMFE_DBUG(0, "dmfe_start_xmit", 0);
	if (db->chip_revision != 0x1A)//1A = DM9000CEP
	{
		if(db->Speed == 10)
			{if (db->tx_pkt_cnt >= 1) return 1;}
		else
			{if (db->tx_pkt_cnt >= 2) return 1;}
	}else
		if (db->tx_pkt_cnt >= 2) return 1;
	
	/* packet counting 发包计数加1*/
	db->tx_pkt_cnt++;

	db->stats.tx_packets++;
	db->stats.tx_bytes+=skb->len;//数据包长度
	if (db->chip_revision != 0x1A)//1A = DM9000CEP
	{
		if (db->Speed == 10)
			{if (db->tx_pkt_cnt >= 1) netif_stop_queue(dev);}/* 停止接收队列 */
		else
			{if (db->tx_pkt_cnt >= 2) netif_stop_queue(dev);}
	}else
		if (db->tx_pkt_cnt >= 2) netif_stop_queue(dev);		

	/* Disable all interrupt 关闭所有中断*/
	iow(db, DM9KS_IMR, DM9KS_DISINTR);

	MDWAH = ior(db,DM9KS_MDWAH);/* 读内存数据写地址寄存器高字节 */
	MDWAL = ior(db,DM9KS_MDWAL);/* 读内存数据写地址寄存器低字节 */

	/* Set TX length to reg. 0xfc & 0xfd 设置数据包长度寄存器*/
	iow(db, DM9KS_TXPLL, (skb->len & 0xff));
	iow(db, DM9KS_TXPLH, (skb->len >> 8) & 0xff);

	/* Move data to TX SRAM ，将sk_buff中数据的地址赋值给SRAM */
	data_ptr = (char *)skb->data;
	
	outb(DM9KS_MWCMD, db->io_addr); // Write data into SRAM trigger
	switch(db->io_mode)/* 选择IO模式 8bit/16bit/32bit */
	{
		case DM9KS_BYTE_MODE:
			for (i = 0; i < skb->len; i++)
				outb((data_ptr[i] & 0xff), db->io_data);
			break;
		case DM9KS_WORD_MODE:
			tmplen = (skb->len + 1) / 2;/* 计算发送长度 */
			for (i = 0; i < tmplen; i++)
        outw(((u16 *)data_ptr)[i], db->io_data);/* 向SRAM中写入数据 */
      break;
    case DM9KS_DWORD_MODE:
      tmplen = (skb->len + 3) / 4;/* 计算发送长度 */
			for (i = 0; i< tmplen; i++)
				outl(((u32 *)data_ptr)[i], db->io_data);/* 向SRAM中写入数据 */
			break;
	}
	
#ifndef ETRANS
	/* Issue TX polling command */
	iow(db, DM9KS_TCR, 0x1); /* Cleared after TX complete*/
#endif

	#ifdef TDBUG /* check TX FIFO pointer */
			MDWAH1 = ior(db,DM9KS_MDWAH);
			MDWAL1 = ior(db,DM9KS_MDWAL);
			tx_ptr = (MDWAH<<8)|MDWAL;
			switch (db->io_mode)
			{
				case DM9KS_BYTE_MODE:
					tx_ptr += skb->len;
					break;
				case DM9KS_WORD_MODE:
					tx_ptr += ((skb->len + 1) / 2)*2;
					break;
				case DM9KS_DWORD_MODE:
					tx_ptr += ((skb->len+3)/4)*4;
					break;
			}
			if (tx_ptr > 0x0bff)
					tx_ptr -= 0x0c00;
			if (tx_ptr != ((MDWAH1<<8)|MDWAL1))
					printk("[dm9ks:TX FIFO ERROR\n");
	#endif
	/* Saved the time stamp 保存当前时间戳 */
	dev->trans_start = jiffies;
	db->cont_rx_pkt_cnt =0;

	/* Free this SKB 释放sk_buff*/
	dev_kfree_skb(skb);

	/* Re-enable interrupt 重新开启全部中断*/
	iow(db, DM9KS_IMR, DM9KS_REGFF);

	return 0;
}

/*
  Stop the interface.
  The interface is stopped when it is brought.
*/
static int dmfe_stop(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	DMFE_DBUG(0, "dmfe_stop", 0);

	/* deleted timer */
	del_timer(&db->timer);

	netif_stop_queue(dev); 

	/* free interrupt */
	free_irq(dev->irq, dev);

	/* RESET devie */
	phy_write(db, 0x00, 0x8000);	/* PHY RESET */
	//iow(db, DM9KS_GPR, 0x01); 	/* Power-Down PHY */
	iow(db, DM9KS_IMR, DM9KS_DISINTR);	/* Disable all interrupt */
	iow(db, DM9KS_RXCR, 0x00);	/* Disable RX */

	/* Dump Statistic counter */
#if FALSE
	printk("\nRX FIFO OVERFLOW %lx\n", db->stats.rx_fifo_errors);
	printk("RX CRC %lx\n", db->stats.rx_crc_errors);
	printk("RX LEN Err %lx\n", db->stats.rx_length_errors);
	printk("RESET %x\n", db->reset_counter);
	printk("RESET: TX Timeout %x\n", db->reset_tx_timeout);
	printk("g_TX_nsr %x\n", g_TX_nsr);
#endif

	return 0;
}

static void dmfe_tx_done(unsigned long unused)
{
	struct net_device *dev = dmfe_dev;
	board_info_t *db = netdev_priv(dev);
	int  nsr;

	DMFE_DBUG(0, "dmfe_tx_done()", 0);
	
	nsr = ior(db, DM9KS_NSR);
	if (nsr & 0x0c)
	{
		if(nsr & 0x04) db->tx_pkt_cnt--;
		if(nsr & 0x08) db->tx_pkt_cnt--;
		if(db->tx_pkt_cnt < 0)
		{
			printk(KERN_DEBUG "DM9KS:tx_pkt_cnt ERROR!!\n");
			while(ior(db,DM9KS_TCR) & 0x1){}
			db->tx_pkt_cnt = 0;
		}
			
	}else{
		while(ior(db,DM9KS_TCR) & 0x1){}
		db->tx_pkt_cnt = 0;
	}
		
	netif_wake_queue(dev);
	
	return;
}

/*
  DM9000 insterrupt handler
  receive the packet to upper layer, free the transmitted packet
*/
static irqreturn_t dmfe_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	board_info_t *db;
	int int_status,i;
	u8 reg_save;

	DMFE_DBUG(0, "dmfe_interrupt()", 0);

	/* A real interrupt coming */
	db = netdev_priv(dev);
	spin_lock(&db->lock);/* 对临界资源加锁 */

	/* Save previous register address 保存寄存器地址*/
	reg_save = inb(db->io_addr);

	/* Disable all interrupt 关闭中断*/
	iow(db, DM9KS_IMR, DM9KS_DISINTR); 

	/* Got DM9000/DM9010 interrupt status */
	int_status = ior(db, DM9KS_ISR);		/* Got ISR */
	iow(db, DM9KS_ISR, int_status);		/* Clear ISR status */ 

	/* Link status change */
	if (int_status & DM9KS_LINK_INTR) 
	{
		netif_stop_queue(dev);
		for(i=0; i<500; i++) /*wait link OK, waiting time =0.5s */
		{
			phy_read(db,0x1);
			if(phy_read(db,0x1) & 0x4) /*Link OK*/
			{
				/* wait for detected Speed */
				for(i=0; i<200;i++)
					udelay(1000);
				/* set media speed */
				if(phy_read(db,0)&0x2000) db->Speed =100;
				else db->Speed =10;
				break;
			}
			udelay(1000);
		}
		netif_wake_queue(dev);
		//printk("[INTR]i=%d speed=%d\n",i, (int)(db->Speed));	
	}
	/* Received the coming packet */
	if (int_status & DM9KS_RX_INTR) 
		dmfe_packet_receive(dev);

	/* Trnasmit Interrupt check */
	if (int_status & DM9KS_TX_INTR)
		dmfe_tx_done(0);
	
	if (db->cont_rx_pkt_cnt>=CONT_RX_PKT_CNT)
	{
		iow(db, DM9KS_IMR, 0xa2);
	}
	else
	{
		/* Re-enable interrupt mask */ 
		iow(db, DM9KS_IMR, DM9KS_REGFF);
	}
	
	/* Restore previous register address */
	outb(reg_save, db->io_addr); 

	spin_unlock(&db->lock); 

	return IRQ_HANDLED;
}

/*
  Get statistics from driver.
*/
static struct net_device_stats * dmfe_get_stats(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	DMFE_DBUG(0, "dmfe_get_stats", 0);
	return &db->stats;
}
/*
 *	Process the ethtool ioctl command
 */
static int dmfe_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	//struct dmfe_board_info *db = dev->priv;
	struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
	u32 ethcmd;

	if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

	switch (ethcmd) 
	{
		case ETHTOOL_GDRVINFO:
			strcpy(info.driver, DRV_NAME);
			strcpy(info.version, DRV_VERSION);

			sprintf(info.bus_info, "ISA 0x%lx %d",dev->base_addr, dev->irq);
			if (copy_to_user(useraddr, &info, sizeof(info)))
				return -EFAULT;
			return 0;
	}

	return -EOPNOTSUPP;
}
/*
  Process the upper socket ioctl command
*/
static int dmfe_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	board_info_t *db = netdev_priv(dev);

  int rc=0;
    	
	DMFE_DBUG(0, "dmfe_do_ioctl()", 0);
	
    	if (!netif_running(dev))
    		return -EINVAL;

    	if (cmd == SIOCETHTOOL)
        rc = dmfe_ethtool_ioctl(dev, (void *) ifr->ifr_data);
	else {
		spin_lock_irq(&db->lock);
		rc = generic_mii_ioctl(&db->mii, if_mii(ifr), cmd, NULL);
		spin_unlock_irq(&db->lock);
	}

	return rc;
}

/* Our watchdog timed out. Called by the networking layer */
static void dmfe_timeout(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	int i;
	
	DMFE_DBUG(0, "dmfe_TX_timeout()", 0);
	printk("TX time-out -- dmfe_timeout().\n");
	db->reset_tx_timeout++;
	db->stats.tx_errors++;
	
#if FALSE
	printk("TX packet count = %d\n", db->tx_pkt_cnt);	
	printk("TX timeout = %d\n", db->reset_tx_timeout);	
	printk("22H=0x%02x  23H=0x%02x\n",ior(db,0x22),ior(db,0x23));
	printk("faH=0x%02x  fbH=0x%02x\n",ior(db,0xfa),ior(db,0xfb));
#endif

	i=0;

	while((i++<100)&&(ior(db,DM9KS_TCR) & 0x01))
	{
		udelay(30);
	}
		
	if(i<100)
	{
			db->tx_pkt_cnt = 0;
			netif_wake_queue(dev);
	}
	else
	{
			dmfe_reset(dev);
	}

}

static void dmfe_reset(struct net_device * dev)
{
	board_info_t *db = netdev_priv(dev);
	u8 reg_save;
	int i;
	/* Save previous register address */
	reg_save = inb(db->io_addr);

	netif_stop_queue(dev); 
	db->reset_counter++;
	dmfe_init_dm9000(dev);
	
	db->Speed =10;
	for(i=0; i<1000; i++) /*wait link OK, waiting time=1 second */
	{
		if(phy_read(db,0x1) & 0x4) /*Link OK*/
		{
			if(phy_read(db,0)&0x2000) db->Speed =100;
			else db->Speed =10;
			break;
		}
		udelay(1000);
	}
	
	netif_wake_queue(dev);
	
	/* Restore previous register address */
	outb(reg_save, db->io_addr);

}
/*
  A periodic timer routine
*/
static void dmfe_timer(unsigned long data)
{
	struct net_device * dev = (struct net_device *)data;
	board_info_t *db = netdev_priv(dev);
	DMFE_DBUG(0, "dmfe_timer()", 0);
	
	if (db->cont_rx_pkt_cnt>=CONT_RX_PKT_CNT)//判断接受包个数是否溢出
	{
		db->cont_rx_pkt_cnt=0;
		iow(db, DM9KS_IMR, DM9KS_REGFF);//设置中断寄存器
	}
	/* Set timer again */
	db->timer.expires = DMFE_TIMER_WUT;
	add_timer(&db->timer);
	
	return;
}


/*
  Received a packet and pass to upper layer
*/
static void dmfe_packet_receive(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	struct sk_buff *skb;
	u8 rxbyte;
	u16 i, GoodPacket, tmplen = 0, MDRAH, MDRAL;
	u32 tmpdata;

	rx_t rx;

	u16 * ptr = (u16*)&rx;
	u8* rdptr;

	DMFE_DBUG(0, "dmfe_packet_receive()", 0);

	db->cont_rx_pkt_cnt=0;
	
	do {
		/*store the value of Memory Data Read address register*/
		MDRAH=ior(db, DM9KS_MDRAH);//数据寄存器高字节
		MDRAL=ior(db, DM9KS_MDRAL);//数据寄存器低字节
		
		ior(db, DM9KS_MRCMDX);		/* Dummy read */
		rxbyte = inb(db->io_data);	/* Got most updated data */

#ifdef CHECKSUM	
		if (rxbyte&0x2)			/* check RX byte */
		{	
      printk("dm9ks: abnormal!\n");
			dmfe_reset(dev); 
			break;	
    }else { 
      if (!(rxbyte&0x1))
				break;	
    }		
#else
		if (rxbyte==0)
			break;
		
		if (rxbyte>1)
		{	
      printk("dm9ks: Rxbyte error!\n");
		  dmfe_reset(dev);
      break;	
    }
#endif

		/* A packet ready now  & Get status/length */
		GoodPacket = TRUE;
		outb(DM9KS_MRCMD, db->io_addr);/* 向寄存器发送读命令 */

		/* Read packet status & length */
		switch (db->io_mode) 
			{
			  case DM9KS_BYTE_MODE: 
 				    *ptr = inb(db->io_data) + 
				               (inb(db->io_data) << 8);
				    *(ptr+1) = inb(db->io_data) + 
					    (inb(db->io_data) << 8);
				    break;
			  case DM9KS_WORD_MODE:
				    *ptr = inw(db->io_data);
				    *(ptr+1)= inw(db->io_data);
				    break;
			  case DM9KS_DWORD_MODE:
				    tmpdata  = inl(db->io_data);
				    *ptr = tmpdata;
				    *(ptr+1) = tmpdata >> 16;
				    break;
			  default:
				    break;
			}

		/* Packet status check */
		if (rx.desc.status & 0xbf)
		{
			GoodPacket = FALSE;
			if (rx.desc.status & 0x01) 
			{
				db->stats.rx_fifo_errors++;
				printk(KERN_INFO"<RX FIFO error>\n");
			}
			if (rx.desc.status & 0x02) 
			{
				db->stats.rx_crc_errors++;
				printk(KERN_INFO"<RX CRC error>\n");
			}
			if (rx.desc.status & 0x80) 
			{
				db->stats.rx_length_errors++;
				printk(KERN_INFO"<RX Length error>\n");
			}
			if (rx.desc.status & 0x08)
				printk(KERN_INFO"<Physical Layer error>\n");
		}

		if (!GoodPacket)
		{
			// drop this packet!!!
			switch (db->io_mode)
			{
				case DM9KS_BYTE_MODE:
			 		for (i=0; i<rx.desc.length; i++)
						inb(db->io_data);
					break;
				case DM9KS_WORD_MODE:
					tmplen = (rx.desc.length + 1) / 2;
					for (i = 0; i < tmplen; i++)
						inw(db->io_data);
					break;
				case DM9KS_DWORD_MODE:
					tmplen = (rx.desc.length + 3) / 4;
					for (i = 0; i < tmplen; i++)
						inl(db->io_data);
					break;
			}
			continue;/*next the packet*/
		}
		
		skb = dev_alloc_skb(rx.desc.length+4);/* 分配sk_buff */
		if (skb == NULL )
		{	
			printk(KERN_INFO "%s: Memory squeeze.\n", dev->name);
			/*re-load the value into Memory data read address register*/
			iow(db,DM9KS_MDRAH,MDRAH);
			iow(db,DM9KS_MDRAL,MDRAL);
			return;
		}
		else
		{
			/* Move data from DM9000 */
			skb->dev = dev;
			skb_reserve(skb, 2);
			rdptr = (u8*)skb_put(skb, rx.desc.length - 4);
			
			/* Read received packet from RX SARM */
			switch (db->io_mode)
			{
				case DM9KS_BYTE_MODE:
			 		for (i=0; i<rx.desc.length; i++)
						rdptr[i]=inb(db->io_data);
					break;
				case DM9KS_WORD_MODE:
					tmplen = (rx.desc.length + 1) / 2;
					for (i = 0; i < tmplen; i++)
						((u16 *)rdptr)[i] = inw(db->io_data);
					break;
				case DM9KS_DWORD_MODE:
					tmplen = (rx.desc.length + 3) / 4;
					for (i = 0; i < tmplen; i++)
						((u32 *)rdptr)[i] = inl(db->io_data);
					break;
			}
		
			/* Pass to upper layer */
			skb->protocol = eth_type_trans(skb,dev);

#ifdef CHECKSUM
		if((rxbyte&0xe0)==0)	/* receive packet no checksum fail */
				skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif
			
			netif_rx(skb);
			dev->last_rx=jiffies;
			db->stats.rx_packets++;
			db->stats.rx_bytes += rx.desc.length;
			db->cont_rx_pkt_cnt++;
#ifdef RDBG /* check RX FIFO pointer */
			u16 MDRAH1, MDRAL1;
			u16 tmp_ptr;
			MDRAH1 = ior(db,DM9KS_MDRAH);
			MDRAL1 = ior(db,DM9KS_MDRAL);
			tmp_ptr = (MDRAH<<8)|MDRAL;
			switch (db->io_mode)
			{
				case DM9KS_BYTE_MODE:
					tmp_ptr += rx.desc.length+4;
					break;
				case DM9KS_WORD_MODE:
					tmp_ptr += ((rx.desc.length+1)/2)*2+4;
					break;
				case DM9KS_DWORD_MODE:
					tmp_ptr += ((rx.desc.length+3)/4)*4+4;
					break;
			}
			if (tmp_ptr >=0x4000)
				tmp_ptr = (tmp_ptr - 0x4000) + 0xc00;
			if (tmp_ptr != ((MDRAH1<<8)|MDRAL1))
				printk("[dm9ks:RX FIFO ERROR\n");
#endif
				
			if (db->cont_rx_pkt_cnt>=CONT_RX_PKT_CNT)
			{
				dmfe_tx_done(0);
				break;
			}
		}
			
	}while((rxbyte & 0x01) == DM9KS_PKT_RDY);
	DMFE_DBUG(0, "[END]dmfe_packet_receive()", 0);
	
}

/*
  Read a word data from SROM
*/
static u16 read_srom_word(board_info_t *db, int offset)
{
	iow(db, DM9KS_EPAR, offset);
	iow(db, DM9KS_EPCR, 0x4);
	while(ior(db, DM9KS_EPCR)&0x1);	/* Wait read complete */
	iow(db, DM9KS_EPCR, 0x0);
	return (ior(db, DM9KS_EPDRL) + (ior(db, DM9KS_EPDRH) << 8) );
}

/*
 *  Set DM9000 multicast address
 */
static void
dm9000_hash_table_unlocked(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i, oft;
	u32 hash_val;
	u16 hash_table[4];
	u8 rcr = RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN;

	//dm9000_dbg(db, 1, "entering %s\n", __func__);

	for (i = 0, oft = DM9000_PAR; i < 6; i++, oft++)
		iow(db, oft, dev->dev_addr[i]);

	/* Clear Hash Table */
	for (i = 0; i < 4; i++)
		hash_table[i] = 0x0;

	/* broadcast address */
	hash_table[3] = 0x8000;

	if (dev->flags & IFF_PROMISC)
		rcr |= RCR_PRMSC;

	if (dev->flags & IFF_ALLMULTI)
		rcr |= RCR_ALL;

	/* the multicast address in Hash Table : 64 bits */
	netdev_for_each_mc_addr(ha, dev) {
		hash_val = ether_crc_le(6, ha->addr) & 0x3f;
		hash_table[hash_val / 16] |= (u16) 1 << (hash_val % 16);
	}

	/* Write the hash table to MAC MD table */
	for (i = 0, oft = DM9000_MAR; i < 4; i++) {
		iow(db, oft++, hash_table[i]);
		iow(db, oft++, hash_table[i] >> 8);
	}

	iow(db, DM9000_RCR, rcr);
}

static void
dm9000_hash_table(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&db->lock, flags);
	dm9000_hash_table_unlocked(dev);
	spin_unlock_irqrestore(&db->lock, flags);
}


static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	board_info_t *db = netdev_priv(dev);//通过net_device结构体获取board_info_t结构体
	return phy_read(db, location);//Read a word from phyxcer
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	board_info_t *db = netdev_priv(dev);
	phy_write(db, location, val);// Write a word to phyxcer
}

/*
   Read a byte from I/O port
*/
u8 ior(board_info_t *db, int reg)
{
	outb(reg, db->io_addr);
	return inb(db->io_data);
}

/*
   Write a byte to I/O port
*/
void iow(board_info_t *db, int reg, u8 value)
{
	outb(reg, db->io_addr);
	outb(value, db->io_data);
}

/*
   Read a word from phyxcer
*/
static u16 phy_read(board_info_t *db, int reg)
{
	/* Fill the phyxcer register into REG_0C */
	iow(db, DM9KS_EPAR, DM9KS_PHY | reg);

	iow(db, DM9KS_EPCR, 0xc); 	/* Issue phyxcer read command */
	while(ior(db, DM9KS_EPCR)&0x1);	/* Wait read complete */
	iow(db, DM9KS_EPCR, 0x0); 	/* Clear phyxcer read command */

	/* The read data keeps on REG_0D & REG_0E */
	return ( ior(db, DM9KS_EPDRH) << 8 ) | ior(db, DM9KS_EPDRL);
	
}

/*
   Write a word to phyxcer
*/
static void phy_write(board_info_t *db, int reg, u16 value)
{
	/* Fill the phyxcer register into REG_0C 往EEPROM PHY地址寄存器，写0x40*/
	iow(db, DM9KS_EPAR, DM9KS_PHY | reg);

	/* Fill the written data into REG_0D & REG_0E EEPROM PHY数据寄存器*/
	iow(db, DM9KS_EPDRL, (value & 0xff));
	iow(db, DM9KS_EPDRH, ( (value >> 8) & 0xff));

	iow(db, DM9KS_EPCR, 0xa);	/* Issue phyxcer write command EEPROM&PHY控制寄存器，配置写PHY*/
	while(ior(db, DM9KS_EPCR)&0x1);	/* 读PHY状态，Wait read PHY complete */
	iow(db, DM9KS_EPCR, 0x0);	/* Clear phyxcer write command */
}

//====dmfe_ethtool_ops member functions====
static void dmfe_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "ISA 0x%lx irq=%d",dev->base_addr, dev->irq);
}

static int dmfe_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	board_info_t *db = netdev_priv(dev);
	spin_lock_irq(&db->lock);
	mii_ethtool_gset(&db->mii, cmd);
	spin_unlock_irq(&db->lock);
	return 0;
}

static int dmfe_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	board_info_t *db = netdev_priv(dev);
	int rc;

	spin_lock_irq(&db->lock);
	rc = mii_ethtool_sset(&db->mii, cmd);
	spin_unlock_irq(&db->lock);
	return rc;
}

static u32 dmfe_get_link(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	return mii_link_ok(&db->mii);
}

static int dmfe_nway_reset(struct net_device *dev)
{
	board_info_t *db = netdev_priv(dev);
	return mii_nway_restart(&db->mii);
}

static struct ethtool_ops dmfe_ethtool_ops = {
	.get_drvinfo		= dmfe_get_drvinfo,
	.get_settings		= dmfe_get_settings,
	.set_settings		= dmfe_set_settings,
	.get_link			= dmfe_get_link,
	.nway_reset		= dmfe_nway_reset,
};


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Davicom DM9000/DM9010 ISA/uP Fast Ethernet Driver");
module_param(mode, int, 0);
module_param(irq, int, 0);
module_param(iobase, int, 0);       
MODULE_PARM_DESC(mode,"Media Speed, 0:10MHD, 1:10MFD, 4:100MHD, 5:100MFD");
MODULE_PARM_DESC(irq,"EtherLink IRQ number");
MODULE_PARM_DESC(iobase, "EtherLink I/O base address");

/* Description: 
   when user used insmod to add module, system invoked init_module()
   to initilize and register.
*/
int __init dm9000c_init(void)
{
	volatile unsigned long *bwscon; // 0x48000000
	volatile unsigned long *bankcon4; // 0x48000014
	unsigned long val;

	iobase = (int)ioremap(0x20000000, 1024);
	irq    = IRQ_EINT7;

	/* 设置S3C2440的memory controller */
	bwscon   = ioremap(0x48000000, 4);
	bankcon4 = ioremap(0x48000014, 4);

	/* DW4[17:16]: 01-16bit(决定 Bank 4 的数据总线宽度)
	 * WS4[18]   : 0-WAIT disable
	 * ST4[19]   : 0 = Not using UB/LB (The pins are dedicated nWBE[3:0])
	 */
	val = *bwscon;
	val &= ~(0xf<<16);//bit16,bit17清零
	val |= (1<<16);//bit16,bit17置1
	*bwscon = val;

	/*
	 * Tacs[14:13]: 发出片选信号之前,多长时间内要先发出地址信号
	 *              DM9000C的片选信号和CMD信号可以同时发出,
	 *              所以它设为0
	 * Tcos[12:11]: 发出片选信号之后,多长时间才能发出读信号nOE
	 *              DM9000C的T1>=0ns, 
	 *              所以它设为0
	 * Tacc[10:8] : 读写信号的脉冲长度, 
	 *              DM9000C的T2>=10ns, 
	 *              所以它设为1, 表示2个hclk周期,hclk=100MHz,就是20ns
	 * Tcoh[7:6]  : 当读信号nOE变为高电平后,片选信号还要维持多长时间
	 *              DM9000C进行写操作时, nWE变为高电平之后, 数据线上的数据还要维持最少3ns
	 *              DM9000C进行读操作时, nOE变为高电平之后, 数据线上的数据在6ns之内会消失
	 *              我们取一个宽松值: 让片选信号在nOE放为高电平后,再维持10ns, 
	 *              所以设为01
	 * Tcah[5:4]  : 当片选信号变为高电平后, 地址信号还要维持多长时间
	 *              DM9000C的片选信号和CMD信号可以同时出现,同时消失
	 *              所以设为0
	 * PMC[1:0]   : 00-正常模式
	 *
	 */
	//*bankcon4 = (1<<8)|(1<<6);	/* 对于DM9000C可以设Tacc为1, 对于DM9000E,Tacc要设大一点,比如最大值7  */
	*bankcon4 = (7<<8)|(1<<6);  /* MINI2440使用DM9000E,Tacc要设大一点 */

	iounmap(bwscon);
	iounmap(bankcon4);
	
	
	switch(mode) {
		case DM9KS_10MHD:
		case DM9KS_100MHD:
		case DM9KS_10MFD:
		case DM9KS_100MFD:
			media_mode = mode;
			break;
		default:
			media_mode = DM9KS_AUTO;
	}
	dmfe_dev = dmfe_probe();
	if(IS_ERR(dmfe_dev))
		return PTR_ERR(dmfe_dev);
	return 0;
}


/* Description: 
   when user used rmmod to delete module, system invoked clean_module()
   to  un-register DEVICE.
*/
void __exit dm9000c_exit(void)
{
	struct net_device *dev = dmfe_dev;
	DMFE_DBUG(0, "clean_module()", 0);
	unregister_netdev(dmfe_dev);
	release_region(dev->base_addr, 2);
	free_netdev(dev);
	iounmap((void *)iobase);
	DMFE_DBUG(0, "clean_module() exit", 0);
}

module_init(dm9000c_init);
module_exit(dm9000c_exit);



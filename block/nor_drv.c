//参考drivers/mtd/maps/physmap.c
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

//定义分区信息
static struct mtd_partition nor_part[] = {
	[0] = {
        .name   = "bootloader_nor",
        .size   = 0x00040000,
		.offset	= 0,
	},
	[1] = {
        .name   = "root_nor",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	},
};
	

static struct map_info *nor_map;
static struct mtd_info *nor_mtd;

static int nor_init(void)
{
	//1.分配一个map_info结构体
	nor_map = kzalloc(sizeof(struct map_info), GFP_KERNEL);
	
	//2.设置：物理基地址，大小，位宽
	nor_map->name = "nor flash";//名字
	nor_map->phys = 0;//物理地址
	nor_map->size = 0x1000000;// 需要>= Nor Flash的实际大小
	nor_map ->bankwidth = 2;//位宽
	nor_map->virt = ioremap(nor_map->phys, nor_map->size);//虚拟地址
	simple_map_init(nor_map);
	
	//3.使用
	printk("use cfi_probe\n");
	nor_mtd = do_map_probe("cfi_probe", nor_map);
	if(!nor_mtd){
		printk("use jedec_probe\n");
		nor_mtd = do_map_probe("jedec_probe", nor_map);
	}
	if(!nor_mtd){
		iounmap(nor_map->virt);
		kfree(nor_map);
		return -EIO;
	}
	//4.add_mtd_partitions
	mtd_device_parse_register(nor_mtd,NULL,NULL,nor_part,2);
	return 0;
}

static void nor_exit(void)
{
	iounmap(nor_map->virt);
	kfree(nor_map);
	map_destroy(nor_mtd);
}

module_init(nor_init);
module_exit(nor_exit);
MODULE_LICENSE("GPL");
//drivers\block\xd.c
//drivers\block\z2ram.c
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>


static struct gendisk *disk;
static struct request_queue *ramblock_queue;//定义一个请求队列
static DEFINE_SPINLOCK(ramblock_lock);      //定义一个自旋锁
#define RAMBLOCK_SIZE (1024*1024)           //定义磁盘空间为1M
static unsigned char *ramblock_buf;

//定义磁盘几何属性
static int ramblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 2;   //2个磁头
	geo->cylinders  = 32;//32个扇区
	geo->sectors = RAMBLOCK_SIZE / geo->heads / geo->cylinders / 512;//计算柱面
	return 0;
}


static struct block_device_operations ramblock_fops = {
	.owner = THIS_MODULE,
	.getgeo = ramblock_getgeo,
};

//当读写磁盘时会调用该函数
static void do_ramblock_request(struct request_queue *q)
{
	static int cnt = 0;
	struct request *req;

	while((req = blk_fetch_request(q)) != NULL)
	{
		//数据传输三要素: 源,目的,长度
		unsigned long offset = blk_rq_pos(req) << 9;//左移9 等价于 乘512（从第几个扇区开始读写数据）
		unsigned long len = blk_rq_cur_bytes(req);; //获取数据长度

		if(rq_data_dir(req) == READ)//判断为读数据
		{
			memcpy(req->buffer, ramblock_buf+offset, len);//复制磁盘数据到内存
		}
		else//写数据
		{
			memcpy(ramblock_buf+offset, req->buffer, len);//内存数据写到磁盘
		}
		__blk_end_request_cur(req, 1);
	}
}


static int ramdisk_init(void)
{
	/*1.分配一个gendisk结构体，次设备号个数16（最多可创建15个分区,次设备号为0时表示整个分区）*/
	disk = alloc_disk(16);
	
	//2.设置
	//2.1分配/设置队列,提供读写能力
	ramblock_queue = blk_init_queue(do_ramblock_request, &ramblock_lock);
	disk->queue = ramblock_queue;
	//2.2设置其他属性
	disk->major = register_blkdev(0, "ramblock");//注册一个块设备驱动，自动分配主设备号  cat /proc/devices
	disk->first_minor = 0;//第一个次设备号从几开始，这里设置从0开始
	sprintf(disk->disk_name, "ramblock");//块设备名字
	disk->fops = &ramblock_fops;
	set_capacity(disk, RAMBLOCK_SIZE/512);//设置磁盘扇区

	//3.硬件相关
	ramblock_buf = kzalloc(RAMBLOCK_SIZE, GFP_KERNEL);
	
	//4.注册
	add_disk(disk);
	
	return 0;
}

static void ramdisk_exit(void)
{
	unregister_blkdev(0, "ramblock");//卸载块设备
	del_gendisk(disk);
	put_disk(disk);
	blk_cleanup_queue(ramblock_queue);
	kfree(ramblock_buf);
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);
MODULE_LICENSE("GPL");
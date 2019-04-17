#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/ip.h>

static struct net_device *vnet_dev;
static unsigned char mac_addr[6] = {0x08,0x01,0x02,0x03,0x04,0x06};

static void emulator_rx_packet(struct sk_buff *skb, struct net_device *dev)
{
	/* 参考LDD3 */
	unsigned char *type;
	struct iphdr *ih;
	__be32 *saddr, *daddr, tmp;
	unsigned char	tmp_dev_addr[ETH_ALEN];
	struct ethhdr *ethhdr;
	
	struct sk_buff *rx_skb;
		
	// 从硬件读出/保存数据
	/* 对调"源/目的"的mac地址 */
	ethhdr = (struct ethhdr *)skb->data;
	memcpy(tmp_dev_addr, ethhdr->h_dest, ETH_ALEN);
	memcpy(ethhdr->h_dest, ethhdr->h_source, ETH_ALEN);
	memcpy(ethhdr->h_source, tmp_dev_addr, ETH_ALEN);

	/* 对调"源/目的"的ip地址 */    
	ih = (struct iphdr *)(skb->data + sizeof(struct ethhdr));
	saddr = &ih->saddr;
	daddr = &ih->daddr;

	tmp = *saddr;
	*saddr = *daddr;
	*daddr = tmp;
	
	//((u8 *)saddr)[2] ^= 1; /* change the third octet (class C) */
	//((u8 *)daddr)[2] ^= 1;
	type = skb->data + sizeof(struct ethhdr) + sizeof(struct iphdr);
	//printk("tx package type = %02x\n", *type);
	// 修改类型, 原来0x8表示ping
	*type = 0; /* 0表示reply */
	
	ih->check = 0;		   /* and rebuild the checksum (ip needs it) */
	ih->check = ip_fast_csum((unsigned char *)ih,ih->ihl);
	
	// 构造一个sk_buff
	rx_skb = dev_alloc_skb(skb->len + 2);
	skb_reserve(rx_skb, 2); /* align IP on 16B boundary */	
	memcpy(skb_put(rx_skb, skb->len), skb->data, skb->len);

	/* Write metadata, and then pass to the receive level */
	rx_skb->dev = dev;
	rx_skb->protocol = eth_type_trans(rx_skb, dev);
	rx_skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;

	// 提交sk_buff
	netif_rx(rx_skb);
}

static int virt_net_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	static int cnt = 0;
	printk("virt_net_send_packet cnt = %d\n", ++cnt);

	/* 对于真实的网卡, 把skb里的数据通过网卡发送出去 */
	netif_stop_queue(dev); /* 停止该网卡的队列 */
    /* ...... */           /* 把skb的数据写入网卡 */

	/* 构造一个假的sk_buff,上报 */
	emulator_rx_packet(skb, dev);

	dev_kfree_skb (skb);   /* 释放skb */
	netif_wake_queue(dev); /* 数据全部发送出去后,唤醒网卡的队列 */

	/* 更新统计信息 */
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	
	return 0;
}


static const struct net_device_ops net_ops = {
	.ndo_start_xmit		= virt_net_send_packet,//发包函数
};

static int virt_net_init(void)
{
	/* 1. 分配一个net_device结构体 
	 * alloc_etherdev -> alloc_netdev_mqs
	 * alloc_netdev -> alloc_netdev_mqs
	*/
	// vnet_dev = alloc_netdev(0, "vnet%d", ether_setup);  
	vnet_dev = alloc_etherdev(0);

	/* 设置 net_device_ops结构体*/
    vnet_dev->netdev_ops = &net_ops;
	/* 设置 MAC地址 */
	memcpy(vnet_dev->dev_addr,mac_addr,6);

    /* 设置 不支持ARP协议：根据目的主机的IP地址，获得其MAC地址。这就是ARP协议要做的事情 */
	vnet_dev->flags           |= IFF_NOARP;

	/* 3. 注册 register_netdev -> register_netdevice(vnet_dev);*/
	register_netdev(vnet_dev);
	
	return 0;
}

static void virt_net_exit(void)
{
	unregister_netdev(vnet_dev);
	free_netdev(vnet_dev);
}

module_init(virt_net_init);
module_exit(virt_net_exit);

MODULE_AUTHOR("ningjw");
MODULE_LICENSE("GPL");

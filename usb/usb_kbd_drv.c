//系统自带驱动位于drivers/hid/usbhid/usbkbd.c
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

static const unsigned char usb_kbd_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
	122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};

struct usb_kbd {
	struct usb_device *usbdev;/* USB鼠标是一种USB设备，需要内嵌一个USB设备结构体来描述其USB属性 */  
	struct input_dev *inputdev;/* USB鼠标同时又是一种输入设备，需要内嵌一个输入设备结构体来描述其输入设备的属性 */  
	
    struct urb *irq_urb;/* 用于中断传输的urb */  
    unsigned char old_data[8];
	signed char *keys_data;/* 普通传输用的地址 */  
	dma_addr_t keys_dma;/* dma 传输用的地址 */  

    struct urb *ctl_urb;/* 用于控制传输的urb */
    struct usb_ctrlrequest *cr;/*用于控制传输*/
    unsigned char *leds_data;
    unsigned char new_leds_data;
    dma_addr_t cr_dma; /*控制请求DMA缓冲地址*/ 
    dma_addr_t leds_dma;
    spinlock_t leds_lock;
};

static struct usb_device_id usb_kbd_id_table[]={
	{USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,  //USB类
                        USB_INTERFACE_SUBCLASS_BOOT, //USB子类
                        USB_INTERFACE_PROTOCOL_KEYBOARD)},//USB协议类型
    { }						/* Terminating entry */
};

static void usb_kbd_irq(struct urb * urb)
{
	struct usb_kbd *kbd = urb->context;
    int i;
    // printk("data = %d,%d,%d,%d,%d,%d,%d,%d\n",
    //        kbd->keys_data[0],kbd->keys_data[1],kbd->keys_data[2],kbd->keys_data[3],
    //        kbd->keys_data[4],kbd->keys_data[5],kbd->keys_data[6],kbd->keys_data[7]);

    for (i = 0; i < 8; i++)//上传crtl、shift、atl 等修饰按键
        input_report_key(kbd->inputdev, usb_kbd_keycode[i + 224], (kbd->keys_data[0] >> i) & 1);

    for(i=2;i<8;i++){
        if(kbd->keys_data[i] != kbd->old_data[i]){
            if(kbd->keys_data[i] )      //按下事件
                input_report_key(kbd->inputdev,usb_kbd_keycode[kbd->keys_data[i]], 1);   
            else  if(kbd->old_data[i]) //松开事件
                input_report_key(kbd->inputdev,usb_kbd_keycode[kbd->old_data[i]], 0);
        }
    }
    memcpy(kbd->old_data, kbd->keys_data, 8);
	input_sync(kbd->inputdev);
	//重新提交urb，以能够响应下次鼠标事件
	usb_submit_urb(kbd->irq_urb, GFP_KERNEL);
}

static void usb_kbd_led(struct urb *urb)
{
 
}

/*如果有事件被响应，我们会调用事件处理层的event函数*/
static int usb_kbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
    unsigned long flags;
    struct usb_kbd *kbd = input_get_drvdata(dev);
    // printk("usb_kbd_event\n");
    if (type != EV_LED)    //不是LED事件就返回
        return -1;
    spin_lock_irqsave(&kbd->leds_lock, flags);//上锁
    //将当前的LED值保存在kbd->newleds中
	kbd->new_leds_data = (test_bit(LED_KANA,    dev->led) << 3) | 
                         (test_bit(LED_COMPOSE, dev->led) << 3) |
		                 (test_bit(LED_SCROLLL, dev->led) << 2) | 
                         (test_bit(LED_CAPSL,   dev->led) << 1) |
		                 (test_bit(LED_NUML,    dev->led));

    if (*(kbd->leds_data) == kbd->new_leds_data){
		return 0;
	}
    *(kbd->leds_data) = kbd->new_leds_data;//更新数据
    
    usb_submit_urb(kbd->ctl_urb, GFP_ATOMIC);//提交urb
    spin_unlock_irqrestore(&kbd->leds_lock, flags);//解锁
    return 0;
}


static int usb_kbd_probe (struct usb_interface *intf,const struct usb_device_id *id)
{
    struct usb_kbd *kbd;
    struct usb_host_interface *interface;
    struct usb_endpoint_descriptor *endpoint;
    int pipe,i;

    spin_lock_init(&kbd->leds_lock);//初始化自旋锁

    kbd = kzalloc(sizeof(struct usb_kbd), GFP_KERNEL);
    kbd->usbdev = interface_to_usbdev(intf);//获取usb_device结构体
    kbd->inputdev = input_allocate_device();//分配input_device结构体
    kbd->irq_urb = usb_alloc_urb(0, GFP_KERNEL);//分配中断urb
    kbd->ctl_urb = usb_alloc_urb(0, GFP_KERNEL);//分配控制urb
    kbd->cr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);//分配控制请求描述符
    kbd->keys_data = usb_alloc_coherent(kbd->usbdev, 8, GFP_ATOMIC, &kbd->keys_dma);//分配中断传输dma传输地址
    kbd->leds_data = usb_alloc_coherent(kbd->usbdev, 1, GFP_ATOMIC, &kbd->leds_dma);//分配控制传输dma传输地址

    //设置 input_device结构体
    kbd->inputdev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) | BIT_MASK(EV_LED);//设置按键事件,重复按键事件与led事件
    kbd->inputdev->ledbit[0] = BIT_MASK(LED_NUML) | BIT_MASK(LED_CAPSL) |
		BIT_MASK(LED_SCROLLL) | BIT_MASK(LED_COMPOSE) |BIT_MASK(LED_KANA);//设置LED事件
    for (i = 0; i < 255; i++)//设置具体的按键事件
		set_bit(usb_kbd_keycode[i], kbd->inputdev->keybit);
    clear_bit(0, kbd->inputdev->keybit);
    kbd->inputdev->event = usb_kbd_event;//当有事件产生时，调用usb_kbd_event函数
    input_register_device(kbd->inputdev);//注册input_device结构体

    //获取端点属性
    interface = intf->cur_altsetting;
    endpoint = &interface->endpoint[0].desc;
    pipe = usb_rcvintpipe(kbd->usbdev, endpoint->bEndpointAddress);

	//设置 中断urb
    usb_fill_int_urb(kbd->irq_urb,        //urb结构体
                     kbd->usbdev,     //usb设备
                     pipe,              //端点管道
                     kbd->keys_data,       //缓存区地址
                     endpoint->wMaxPacketSize,//数据长度
                     usb_kbd_irq,     //usb中断处理函数
                     kbd,
                     endpoint->bInterval);//中断间隔时间
    kbd->irq_urb->transfer_dma = kbd->keys_dma;//设置DMA地址
    kbd->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;//设置使用DMA地址
    usb_submit_urb(kbd->irq_urb, GFP_KERNEL);//提交urb

    //设置 控制请求描述符usb_ctrlrequest
    kbd->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;//设定传输方向、请求类型等
	kbd->cr->bRequest = USB_REQ_SET_CONFIGURATION; //指定请求类型
	kbd->cr->wValue = cpu_to_le16(0x200);//即将写到寄存器的数据
	kbd->cr->wIndex = cpu_to_le16(interface->desc.bInterfaceNumber);//接口数量，也就是寄存器的偏移地址
	kbd->cr->wLength = cpu_to_le16(1);//数据传输阶段传输多少个字节

    //设置 控制urb
    usb_fill_control_urb(kbd->ctl_urb, 
                         kbd->usbdev, 
                         usb_sndctrlpipe(kbd->usbdev, 0),//the endpoint pipe
			             (void *) kbd->cr,//setup_packet buffer
                         kbd->leds_data, //transfer buffer
                         1,              //length of the transfer buffer
			             usb_kbd_led,    //pointer to the usb_complete_t function
                         kbd);
	kbd->ctl_urb->transfer_dma = kbd->leds_dma;
	kbd->ctl_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
    
    input_set_drvdata(kbd->inputdev, kbd);//设置私有数据
    usb_set_intfdata(intf, kbd);
    return 0;
}

static void usb_kbd_disconnect (struct usb_interface *intf)
{
    struct usb_kbd *kbd = usb_get_intfdata (intf);
	usb_set_intfdata(intf, NULL);
    if (kbd) {
        usb_free_urb(kbd->irq_urb);
        usb_free_urb(kbd->ctl_urb);
        kfree(kbd->cr);
        usb_free_coherent(kbd->usbdev, 8, kbd->keys_data, kbd->keys_dma);
        usb_free_coherent(kbd->usbdev, 1, kbd->leds_data, kbd->leds_dma);

        input_unregister_device(kbd->inputdev);
        usb_kill_urb(kbd->irq_urb);
        usb_kill_urb(kbd->ctl_urb);
		kfree(kbd);
    }
}

static struct usb_driver usb_kbd_driver = {
	.name = "usb_kbd",
	.probe = usb_kbd_probe,
	.disconnect = usb_kbd_disconnect,
	.id_table = usb_kbd_id_table,
};

module_usb_driver(usb_kbd_driver);
MODULE_LICENSE("GPL");


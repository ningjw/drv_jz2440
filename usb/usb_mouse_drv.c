//系统自带的usb鼠标驱动位于：drivers/hid/usbhid/usbmouse.c
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

struct usb_mouse {
	struct usb_device *usbdev;/* USB鼠标是一种USB设备，需要内嵌一个USB设备结构体来描述其USB属性 */  
	struct input_dev *inputdev;/* USB鼠标同时又是一种输入设备，需要内嵌一个输入设备结构体来描述其输入设备的属性 */  
	struct urb *urb;/* URB 请求包结构体，用于传送数据 */  

	signed char *data;/* 普通传输用的地址 */  
	dma_addr_t data_dma;/* dma 传输用的地址 */  
};

static struct usb_device_id usb_mouse_id_table[]={
	{USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,  //USB类
                        USB_INTERFACE_SUBCLASS_BOOT, //USB子类
                        USB_INTERFACE_PROTOCOL_MOUSE)},//USB协议类型
};

static void usb_mouse_irq(struct urb * urb)
{
	struct usb_mouse *mouse = urb->context;
//	printk("mouse->data= %x,%x,%x,%x\n",mouse->data[0],mouse->data[1],mouse->data[2],mouse->data[3]);
    input_report_key(mouse->inputdev, BTN_LEFT,   mouse->data[0] & 0x01);//鼠标左键
	input_report_key(mouse->inputdev, BTN_RIGHT,  mouse->data[0] & 0x02);//鼠标右键
	input_report_key(mouse->inputdev, BTN_MIDDLE, mouse->data[0] & 0x04);//鼠标中轮
	input_report_key(mouse->inputdev, BTN_SIDE,   mouse->data[0] & 0x08);
	input_report_key(mouse->inputdev, BTN_EXTRA,  mouse->data[0] & 0x10);

	input_report_rel(mouse->inputdev, REL_X,     mouse->data[1]);//鼠标水平位移
	input_report_rel(mouse->inputdev, REL_Y,     mouse->data[2]);//鼠标垂直位移
	input_report_rel(mouse->inputdev, REL_WHEEL, mouse->data[3]);//鼠标滚轮位移

	input_sync(mouse->inputdev);
	//重新提交urb，以能够响应下次鼠标事件
	usb_submit_urb(mouse->urb, GFP_KERNEL);
}


static int usb_mouse_probe (struct usb_interface *intf,const struct usb_device_id *id)
{
    struct usb_mouse *mouse;
    struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
    int pipe;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

    mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
    mouse->usbdev = interface_to_usbdev(intf);//获取usb_device结构体

    //1.分配input_device结构体
    mouse->inputdev = input_allocate_device();
	//2.1 设置按键事件与相对位移事件
    mouse->inputdev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	//2.2 设置具体的按键事件
	mouse->inputdev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | 
                      BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);
    //2.3 设置鼠具体的相对位移事件
    mouse->inputdev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) | BIT_MASK(REL_WHEEL);
	//3.注册
	input_register_device(mouse->inputdev);

	//4.硬件相关操作
	//源:USB设备某个端点
	pipe = usb_rcvintpipe(mouse->usbdev, endpoint->bEndpointAddress);

	//分配，设置urb
    mouse->urb = usb_alloc_urb(0, GFP_KERNEL);
	usb_fill_int_urb(mouse->urb,        //urb结构体
                     mouse->usbdev,     //usb设备
                     pipe,              //端点管道
                     mouse->data,       //缓存区地址
                     endpoint->wMaxPacketSize,//数据长度
                     usb_mouse_irq,     //usb中断处理函数
                     mouse,
                     endpoint->bInterval);//中断间隔时间
    mouse->data = usb_alloc_coherent(mouse->usbdev, 8, GFP_ATOMIC, &mouse->data_dma);//分配dma传输地址
	mouse->urb->transfer_dma = mouse->data_dma;//设置DMA地址
	mouse->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;//设置使用DMA地址

	usb_submit_urb(mouse->urb, GFP_KERNEL);//提交urb

    usb_set_intfdata(intf, mouse);
	return 0;
}

static void usb_mouse_disconnect (struct usb_interface *intf)
{
    struct usb_mouse *mouse = usb_get_intfdata (intf);
	usb_set_intfdata(intf, NULL);
    if (mouse) {
        usb_kill_urb(mouse->urb);
		input_unregister_device(mouse->inputdev);
        input_free_device(mouse->inputdev);
		usb_free_urb(mouse->urb);
		usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
		kfree(mouse);
    }
}

static struct usb_driver usb_mouse_driver = {
	.name = "usb_mouse",
	.probe = usb_mouse_probe,
	.disconnect = usb_mouse_disconnect,
	.id_table = usb_mouse_id_table,
};

module_usb_driver(usb_mouse_driver);
MODULE_LICENSE("GPL");


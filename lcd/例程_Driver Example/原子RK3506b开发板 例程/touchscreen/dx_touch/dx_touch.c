#include "linux/stddef.h"
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/input/mt.h>
#include <linux/random.h>


#define lcdx    480
#define lcdy    640

#define FT_SWAP(x, y)                 do{\
                                         typeof(x) z = x;\
                                         x = y;\
                                         y = z;\
                                       }while (0)

#if 1
#define FT_DEBUG(fmt,arg...)  printk("DX_TOUCH:%s %d "fmt"",__FUNCTION__,__LINE__,##arg);
#else
#define FT_DEBUG(fmt,arg...)
#endif

struct dx_touch_dev {
    struct i2c_client *client;
    struct input_dev *input_dev;
    int rst_pin;
    int irq_pin;
    u32 abs_x_max;
    u32 abs_y_max;
    int irq;
};

s32 dx_touch_i2c_read(struct i2c_client *client,u8 *addr,u8 addr_len, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = addr_len;
    msgs[0].buf   = &addr[0];
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len;
    msgs[1].buf   = &buf[0];

    ret = i2c_transfer(client->adapter, msgs, 2);
    if(ret == 2)return 0;

    if(addr_len == 2){
        FT_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        FT_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    
    return -1;
}

s32 dx_touch_i2c_write(struct i2c_client *client, u8 *addr, u8 addr_len, u8 *buf,s32 len)
{
    struct i2c_msg msg;
    s32 ret = -1;
    u8 *temp_buf;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len+addr_len;

    temp_buf= kzalloc(msg.len, GFP_KERNEL);
    if (!temp_buf){
        goto error;
    }
    
    // 装填地址
    memcpy(temp_buf, addr, addr_len);
    // 装填数据
    memcpy(temp_buf + addr_len, buf, len);
    msg.buf = temp_buf;

    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret == 1) {
        kfree(temp_buf);
        return 0;
    }

error:
    if(addr_len == 2){
        FT_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        FT_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    if (temp_buf)
        kfree(temp_buf);
    return -1;
}

static irqreturn_t dx_touch_irq_handler(int irq, void *dev_id)
{
    s32 ret = -1;
    struct dx_touch_dev *ts = dev_id;
    u8 addr[1] = {0x02};
    u8 point_data[1+6*5]={0};//1个状态位置+5个触摸点，一个点是6个数据组成
    u8 touch_num = 0;
    u8 *touch_data;
    int i = 0;
    int event_flag, touch_id, input_x, input_y;

    FT_DEBUG("irq");

    ret = dx_touch_i2c_read(ts->client, addr,sizeof(addr), point_data, sizeof(point_data));
    if (ret < 0){
        FT_DEBUG("I2C write end_cmd error!");
    }
    touch_num = point_data[0]&0x0f;
    FT_DEBUG("touch_num:%d",touch_num);

    //获取

    for(i=0; i<5; i++){
        //获取点
        touch_data = &point_data[1+6*i];
        /*
        00b: Put Down 
        01b: Put Up 
        10b: Contact 
        11b: Reserved
        */
        event_flag = touch_data[0] >> 6;
        if(event_flag == 0x03)continue; 
        touch_id = touch_data[2] >> 4;

        FT_DEBUG("i:%d touch_id:%d event_flag:%d",i,touch_id,event_flag);
     //  input_x  = ((touch_data[0]&0x0f)<<8) | touch_data[1];
     input_x  = 480-(((touch_data[0]&0x0f)<<8) | touch_data[1]);
        input_y  = ((touch_data[2]&0x0f)<<8) | touch_data[3];

     //    FT_SWAP(input_x,input_y);//横屏时打开，竖屏时要屏蔽
        FT_DEBUG("i:%d,x:%d,y:%d",i,input_x,input_y);
        // 设定输入设备的触摸槽位
        input_mt_slot(ts->input_dev, touch_id);

        if(event_flag == 0){
            // 如果是按下上报按下和坐标
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
        }else if (event_flag == 2){
            // 如果是长按直接上报数据
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
        }else if(event_flag == 1){
            // 触摸抬起，上报事件
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
        }
    }

    // 报告输入设备的指针仿真信息
    input_mt_report_pointer_emulation(ts->input_dev, true);

    // 同步输入事件
    input_sync(ts->input_dev);

    return IRQ_HANDLED;
}


s32 ft5xx6_read_version(struct i2c_client *client)
{
    s32 ret = -1;
    u8 addr[1] = {0xA1};
    u8 buf[3] = {0};


    ret = dx_touch_i2c_read(client, addr,sizeof(addr), buf, sizeof(buf));
    if (ret < 0){
        FT_DEBUG("GTP read version failed");
        return ret;
    }

    if (buf[5] == 0x00){
        FT_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    else{
        FT_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    return ret;
}

static int dx_touch_ts_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    int ret;
    struct dx_touch_dev *ts;
    struct device_node *np = client->dev.of_node;
    // 打印调试信息
    FT_DEBUG("locat");

    // ts = kzalloc(sizeof(*ts), GFP_KERNEL);
    ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
    if (ts == NULL){
        dev_err(&client->dev, "Alloc GFP_KERNEL memory failed.");
        return -ENOMEM;
    }
    ts->client = client;
    i2c_set_clientdata(client, ts);

    if (of_property_read_u32(np, "max-x", &ts->abs_x_max)) {
    	dev_err(&client->dev, "no max-x defined\n");
    	return -EINVAL;
    }
    FT_DEBUG("abs_x_max:%d",ts->abs_x_max);

    if (of_property_read_u32(np, "max-y", &ts->abs_y_max)) {
    	dev_err(&client->dev, "no max-y defined\n");
    	return -EINVAL;
    }
    FT_DEBUG("abs_x_max:%d",ts->abs_y_max);

    //找复位gpio
    ts->rst_pin = of_get_named_gpio(np, "reset-gpio", 0);
    //申请复位gpio
    ret = devm_gpio_request(&client->dev,ts->rst_pin,"dx touch touch gpio");
    if (ret < 0){
        dev_err(&client->dev, "gpio request failed.");
        return -ENOMEM;
    }

    //找中断引进
    ts->irq_pin = of_get_named_gpio(np, "touch-gpio", 0);
    /* 申请使用管脚 */
    ret = devm_gpio_request_one(&client->dev, ts->irq_pin,
                GPIOF_IN, "dx touch touch gpio");
    if (ret < 0)
        return ret;

    gpio_direction_output(ts->rst_pin,1);
    msleep(2); 
    gpio_direction_output(ts->irq_pin,0);
    msleep(2); 
    gpio_direction_output(ts->rst_pin,1);
    msleep(16); 
    gpio_direction_output(ts->irq_pin, 1);
  //  gpio_direction_output(ts->irq_pin, 0);
    msleep(50);

    //申请中断
    ts->irq = gpio_to_irq(ts->irq_pin); 
    if(ts->irq){
        ret = devm_request_threaded_irq(&(client->dev), ts->irq, NULL, 
            dx_touch_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT , 
            client->name, ts);
        if (ret != 0) {
            FT_DEBUG("Cannot allocate ts INT!ERRNO:%d\n", ret);
            return ret;
        }
    }

    // 分配输入设备对象
    ts->input_dev = devm_input_allocate_device(&client->dev);
    if (!ts->input_dev) {
        dev_err(&client->dev, "Failed to allocate input device.\n");
        return -ENOMEM;
    }

    // 设置输入设备的名称和总线类型
    ts->input_dev->name = "dx touch screen";
    ts->input_dev->id.bustype = BUS_I2C;

    /*设置触摸 x 和 y 的最大值*/
    // 设置输入设备的绝对位置参数
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, lcdx, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, lcdy, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

    // 初始化多点触摸设备的槽位
    ret = input_mt_init_slots(ts->input_dev, 5, INPUT_MT_DIRECT);
    if (ret) {
        dev_err(&client->dev, "Input mt init error\n");
        return ret;
    }

    // 注册输入设备
    ret = input_register_device(ts->input_dev);
    if (ret)
        return ret;

    ft5xx6_read_version(client);

    
    return 0;
}

static void dx_touch_ts_remove(struct i2c_client *client)
{
    struct dx_touch_dev *ts = i2c_get_clientdata(client);
    FT_DEBUG("locat");
    input_unregister_device(ts->input_dev);
  //  return 0;
}

static const struct of_device_id dx_touch_of_match[] = {
    { .compatible = "dx,touch", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dx_touch_of_match);


static struct i2c_driver dx_touch_ts_driver = {
    .probe      = dx_touch_ts_probe,
    .remove     = dx_touch_ts_remove,
    .driver = {
        .name     = "dx-touch",
	 .of_match_table = of_match_ptr(dx_touch_of_match),
    },
};

static int __init dx_ts_init(void)
{
    FT_DEBUG("locat");
    return i2c_add_driver(&dx_touch_ts_driver);
}

static void __exit dx_ts_exit(void)
{
    FT_DEBUG("locat");
	i2c_del_driver(&dx_touch_ts_driver);
}

module_init(dx_ts_init);
module_exit(dx_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dx touch driver");
MODULE_AUTHOR("pzjiao2008@126.com");
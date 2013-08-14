/*
 *  lis3lv02d.c - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/i2c.h>
#include <linux/sensor-input.h>
#include <asm/atomic.h>
#include "lis3lv02d.h"

static struct i2c_client *g_client;

static struct sensor_axis axis;

static struct sensor_axis lis3lv02d_axis_conversion[] = {
	{2, -1, -3},
	{-1, -2, 3},
};

static unsigned short normal_i2c[] = {
	0x1c,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

static int lis3lv02d_do_probe( struct i2c_adapter *adapter, int addr, int kind );

static unsigned int platform;
module_param(platform, bool, 0);	//ttc default, td set to 1
MODULE_PARM_DESC(platform, "Set axis according to platform.");
static unsigned int busnum = -1;
module_param(busnum, bool, 0);	//set bus num
MODULE_PARM_DESC(busnum, "Set bus num.");

static const unsigned short	lis3lv02d_i2c = 0x1c;

static int lis3lv02d_read(u8 reg, u8 *pval)
{
	int ret;
	int status;

	if (g_client == NULL)	/* No global client pointer? */
		return -1;
	ret = i2c_smbus_read_byte_data(g_client, reg);
	if (ret >= 0) {
		*pval = ret;
		status = 0;
	} else {
		status = -EIO;
	}

	return status;
}

static int lis3lv02d_write(u8 reg, u8 val)
{
	int ret;
	int status;

	if (g_client == NULL)	/* No global client pointer? */
		return -1;
	ret = i2c_smbus_write_byte_data(g_client, reg, val);
	if (ret == 0)
		status = 0;
	else
		status = -EIO;

	return status;
}

static s16 lis3lv02d_read_16(int reg)
{
	u8 lo, hi;

	lis3lv02d_read(reg, &lo);
	lis3lv02d_read(reg + 1, &hi);
	/* In "12 bit right justified" mode, bit 6, bit 7, bit 8 = bit 5 */
	return (s16)((hi << 8) | lo);
}

static inline void lis3lv02d_poweroff(void)
{
	/* disable X,Y,Z axis and power down */
	lis3lv02d_write(CTRL_REG1, 0x00);
}

static void lis3lv02d_poweron(void)
{
	u8 val;

	lis3lv02d_write(CTRL_REG1, 
		CTRL1_PD1|CTRL1_PD0|CTRL1_Xen|CTRL1_Yen|CTRL1_Zen);

	lis3lv02d_write(FF_WU_CFG, 0);
	/*
	 * BDU: LSB and MSB values are not updated until both have been read.
	 *      So the value read will always be correct.
	 * IEN: Interrupt for free-fall and DD, not for data-ready.
	 */
	lis3lv02d_read(CTRL_REG2, &val);
	val |= CTRL2_BDU | CTRL2_IEN;
	lis3lv02d_write(CTRL_REG2, val);
}

static inline int lis3lv02d_i2c_get_axis(s8 axis, int hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

void lis3lv02d_get_xyz(int *x, int *y, int *z)
{
	int position[3];

	position[0] = lis3lv02d_read_16(OUTX_L)/8;;
	position[1] = lis3lv02d_read_16(OUTY_L)/8;
	position[2] = lis3lv02d_read_16(OUTZ_L)/8;

	*x = lis3lv02d_i2c_get_axis(axis.x, position);
	*y = lis3lv02d_i2c_get_axis(axis.y, position);
	*z = lis3lv02d_i2c_get_axis(axis.z, position);
}
EXPORT_SYMBOL(lis3lv02d_get_xyz);

static void lis3lv02d_report(struct input_dev *idev)
{
	int x, y, z;
	int position[3];
	s8 status;

	lis3lv02d_read(STATUS_REG, &status);
	if((status & 0x7) == 0x7) {
		position[0] = lis3lv02d_read_16(OUTX_L)/8;;
		position[1] = lis3lv02d_read_16(OUTY_L)/8;
		position[2] = lis3lv02d_read_16(OUTZ_L)/8;

		x = lis3lv02d_i2c_get_axis(axis.x, position);
		y = lis3lv02d_i2c_get_axis(axis.y, position);
		z = lis3lv02d_i2c_get_axis(axis.z, position);

		input_report_abs(idev, ABS_X, x);
		input_report_abs(idev, ABS_Y, y);
		input_report_abs(idev, ABS_Z, z);
		input_sync(idev);
	}
}

#ifdef	CONFIG_PROC_FS
#define	LIS331DL_PROC_FILE	"driver/lis3lv02d"
static struct proc_dir_entry *lis3lv02d_proc_file;
static int index;

static ssize_t lis3lv02d_proc_read(struct file *filp,
		char *buffer, size_t length, loff_t *offset)
{
	u8 reg_val;

	if ((index < 0) || (index > 0x3f))
		return 0;

	lis3lv02d_read(index, &reg_val);
	printk(KERN_INFO "register 0x%x: 0x%x\n", index, reg_val);
	return 0;
}

static ssize_t lis3lv02d_proc_write(struct file *filp,
		const char *buff, size_t len, loff_t *off)
{
	u8 reg_val;
	char messages[256], vol[256];

	if (len > 256)
		len = 256;

	if (copy_from_user(messages, buff, len))
		return -EFAULT;

	if ('-' == messages[0]) {
		/* set the register index */
		memcpy(vol, messages+1, len-1);
		index = (int) simple_strtoul(vol, NULL, 16);
	} else {
	    /* set the register value */
		reg_val = (int)simple_strtoul(messages, NULL, 16);
		lis3lv02d_write(index, reg_val & 0xFF);
	}

	return len;
}

static struct file_operations lis3lv02d_proc_ops = {
	.read = lis3lv02d_proc_read,
	.write = lis3lv02d_proc_write,
};

static void create_lis3lv02d_proc_file(void)
{
	lis3lv02d_proc_file = create_proc_entry(LIS331DL_PROC_FILE, 0644, NULL);
	if (lis3lv02d_proc_file) {
		lis3lv02d_proc_file->owner = THIS_MODULE;
		lis3lv02d_proc_file->proc_fops = &lis3lv02d_proc_ops;
	} else
		printk(KERN_INFO "proc file create failed!\n");
}

static void remove_lis3lv02d_proc_file(void)
{
	remove_proc_entry(LIS331DL_PROC_FILE, NULL);
}

#endif

static int  lis3lv02d_probe(struct i2c_client *client)
{
	int chip_id;

	printk("********platform:%d***busnum: %d*****\n", platform, busnum);

	chip_id = i2c_smbus_read_byte_data(client, 0x0F);
	if (chip_id < 0) {
		printk(KERN_WARNING "lis3lv02d unavailable!\n");
		g_client = NULL;
		return -ENXIO;
	} else {
		printk(KERN_INFO "lis3lv02d(chip id:0x%02x) detected.\n", chip_id);
	}

	g_client = client;

	sensor_input_add(INPUT_G_SENSOR, "lis3lv02d", lis3lv02d_report, NULL, lis3lv02d_poweron,
			lis3lv02d_poweroff);

#ifdef	CONFIG_PROC_FS
	create_lis3lv02d_proc_file();
#endif
	return 0;
}

static int lis3lv02d_attach(struct i2c_adapter *adap)
{
	int adap_id = i2c_adapter_id(adap);

	if (busnum == -1)
		return i2c_probe(adap, &addr_data, &lis3lv02d_do_probe);
	if (adap_id == busnum)
		return i2c_probe(adap, &addr_data, &lis3lv02d_do_probe);
}

static int lis3lv02d_remove(struct i2c_adapter *adap)
{
	printk("rmmod lis3lv02d_i2c\n");
	sensor_input_del("lis3lv02d");

#ifdef	CONFIG_PROC_FS
	remove_lis3lv02d_proc_file();
#endif
	return 0;
}

static int lis3lv02d_detach(struct i2c_client *client)
{
	int err;

	err=i2c_detach_client(client);
	if (err)
		printk(KERN_ERR "failed to detach lis3lv02d client\n");
	else {
		kfree(client);
	}
	return err;
}

static const struct i2c_device_id lis3lv02d_id[] = {
	{ "lis3lv02d", 0 },
	{ }
};

static struct i2c_driver lis3lv02d_driver = {
	.driver = {
		.name	= "lis3lv02d",
	},
	.id_table 	= lis3lv02d_id,
	.attach_adapter = lis3lv02d_attach,
	.detach_adapter = lis3lv02d_remove,
	.detach_client = lis3lv02d_detach,
};

static int
lis3lv02d_do_probe( struct i2c_adapter *adapter, int addr, int kind )
{
	struct i2c_client *cl;

	if( !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA
				     | I2C_FUNC_SMBUS_WRITE_BYTE) )
		return 0;

	if( !(cl=kzalloc(sizeof(*cl), GFP_KERNEL)) )
		return -ENOMEM;

	cl->addr = addr;
	cl->adapter = adapter;
	cl->driver = &lis3lv02d_driver;
	cl->flags = 0;

	if( addr == lis3lv02d_i2c )
		return lis3lv02d_probe(cl);
	return 0;
}

static int  lis3lv02d_plat_probe(struct platform_device *pdev)
{
	if (pdev->dev.platform_data)
		platform = (*(int *)pdev->dev.platform_data);
	axis = lis3lv02d_axis_conversion[platform];
	return i2c_add_driver(&lis3lv02d_driver);
}

static void  lis3lv02d_plat_remove(struct platform_device *pdev)
{
	i2c_del_driver(&lis3lv02d_driver);
}

static struct platform_driver lis3lv02d_plat_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "lis3lv02d",
	},
	.probe = lis3lv02d_plat_probe,
	.remove = lis3lv02d_plat_remove,
};

static int __init lis3lv02d_i2c_init(void)
{
	return platform_driver_register(&lis3lv02d_plat_driver);
}

static void __exit lis3lv02d_i2c_exit(void)
{
	platform_driver_unregister(&lis3lv02d_plat_driver);
}


MODULE_DESCRIPTION("ST LIS3LV02Dx three-axis digital accelerometer (I2C) driver");
MODULE_AUTHOR("Bin Yang <bin.yang@marvell.com>");
MODULE_LICENSE("GPL");

module_init(lis3lv02d_i2c_init);
module_exit(lis3lv02d_i2c_exit);

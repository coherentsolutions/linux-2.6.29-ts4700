/*
 *	drivers/switch/switch_headset.c
 *
 *	headset detect driver for Android
 *
 *	Copyright (C) 2009, Marvell Corporation (xjian@Marvell.com)
 *	Author: Raul Xiong <xjian@marvell.com>
 *				 Mike Lockwood <lockwood@android.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/pxa910-squ.h>
#include <mach/sanremo.h>
#include <linux/kthread.h>
#include <linux/delay.h>

struct headset_switch_data {
	struct switch_dev sdev;
	const char *name_on;
	const char *name_off;
	const char *state_on;
	const char *state_off;
	int irq;
	struct work_struct work;
	int state;
};

struct headset_switch_data *psw_data;
static int sanremo_chip_id;

static void headset_update_state(int state)
{
	if (psw_data != NULL) {
		psw_data->state = state;
		schedule_work(&psw_data->work);
	}
}

static void headset_switch_work(struct work_struct *work)
{
	struct headset_switch_data	*data =
		container_of(work, struct headset_switch_data, work);

	printk(KERN_INFO "headset_switch_work to %d \n", data->state);
	switch_set_state(&data->sdev, data->state);
}

static ssize_t switch_headset_print_state(struct switch_dev *sdev, char *buf)
{
	struct headset_switch_data	*switch_data =
		container_of(sdev, struct headset_switch_data, sdev);
	const char *state;
	if (switch_get_state(sdev))
		state = switch_data->state_on;
	else
		state = switch_data->state_off;

	if (state)
		return sprintf(buf, "%s\n", state);
	return -1;
}

static int headset_switch_probe(struct platform_device *pdev)
{
	struct headset_switch_platform_data *pdata = pdev->dev.platform_data;
	struct headset_switch_data *switch_data;
	int ret = 0;
	if (!pdata)
		return -EBUSY;

	switch_data = kzalloc(sizeof(struct headset_switch_data), GFP_KERNEL);
	if (!switch_data)
		return -ENOMEM;
	switch_data->sdev.name = pdata->name;
	switch_data->name_on = pdata->name_on;
	switch_data->name_off = pdata->name_off;
	switch_data->state_on = pdata->state_on;
	switch_data->state_off = pdata->state_off;
	switch_data->sdev.print_state = switch_headset_print_state;
	psw_data = switch_data;
	ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0)
		goto err_switch_dev_register;

	INIT_WORK(&switch_data->work, headset_switch_work);

	pdata->enable_detect(headset_update_state, 1);

	/* Perform initial detection */
	headset_switch_work(&switch_data->work);

	return 0;

err_switch_dev_register:
	kfree(switch_data);

	return ret;
}

static int __devexit headset_switch_remove(struct platform_device *pdev)
{
	struct headset_switch_data *switch_data = platform_get_drvdata(pdev);

	cancel_work_sync(&switch_data->work);

	switch_dev_unregister(&switch_data->sdev);
	kfree(switch_data);

	return 0;
}

static struct platform_driver headset_switch_driver = {
	.probe		= headset_switch_probe,
	.remove		= __devexit_p(headset_switch_remove),
	.driver		= {
		.name	= "headset",
		.owner	= THIS_MODULE,
	},
};

static int __init headset_switch_init(void)
{
	return platform_driver_register(&headset_switch_driver);
}

static void __exit headset_switch_exit(void)
{
	platform_driver_unregister(&headset_switch_driver);
}

module_init(headset_switch_init);
module_exit(headset_switch_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("headset Switch driver");
MODULE_LICENSE("GPL");

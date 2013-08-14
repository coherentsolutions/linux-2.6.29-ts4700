/*
 * rfkill power contorl for Marvell sd8xxx wlan/bt
 *
 * Copyright (C) 2009 Marvell, Inc.
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
 *
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/sd8x_rfkill.h>
#include <mach/gpio.h>
#include <mach/mmc.h>
#define SD8X_DEV_NAME "sd8x-rfkill"

struct sd8x_rfkill_data {
	enum rfkill_type type;
	struct sd8x_rfkill_platform_data *pdata;
};

int sd8x_sdh_init(struct device *dev,
	irq_handler_t detect_irq, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxasdh_platform_data *pdata =
		(struct pxasdh_platform_data *)pdev->dev.platform_data;

	if (pdata->rfkill_pdata) {
		struct sd8x_rfkill_platform_data *rfkill_pdata =
			(struct sd8x_rfkill_platform_data *)(pdata->rfkill_pdata);
		rfkill_pdata->detect_irq = detect_irq;
		rfkill_pdata->mmc_data = data;
	}

	return 0;
}
EXPORT_SYMBOL(sd8x_sdh_init);

void* add_sd8x_rfkill_device(int gpio_power_down, int gpio_reset)
{
        int ret;
	struct platform_device *pdev = NULL;
	struct sd8x_rfkill_platform_data *pdata = NULL;

	pdata = kzalloc(sizeof(struct sd8x_rfkill_platform_data), GFP_KERNEL);
	if (!pdata) {
		printk(KERN_CRIT "no memory\n");
		goto err_out;
	}
	pdata->gpio_power_down = gpio_power_down;
	pdata->gpio_reset = gpio_reset;

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!pdev) {
		printk(KERN_CRIT "no memory\n");
		goto err_out;
	}
	pdev->name = SD8X_DEV_NAME;
	pdev->id = -1,
	pdev->dev.platform_data = pdata;

        ret = platform_device_register(pdev);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to register device: %d\n", ret);
		goto err_out;
	}
	return pdata;

err_out:
	if (pdata)
		kfree(pdata);
	if (pdev)
		kfree(pdev);
	return NULL;
}
EXPORT_SYMBOL(add_sd8x_rfkill_device);

static int sd8x_power_on(struct sd8x_rfkill_platform_data *pdata, int on)
{
	int gpio_power_down = pdata->gpio_power_down;
	int gpio_reset = pdata->gpio_reset;

	pr_debug("%s: on=%d\n", __FUNCTION__, on);
	if (gpio_request(gpio_power_down, "sd8xxx power down")) {
		printk(KERN_INFO "gpio %d request failed\n", gpio_power_down);
		return -1;
	}

	if(gpio_reset && gpio_request(gpio_reset, "sd8xxx reset")) {
		printk(KERN_INFO "gpio %d request failed\n", gpio_reset);
		gpio_free(gpio_power_down);
		return -1;
	}

	if (on) {
		gpio_direction_output(gpio_power_down, 1);
		if(gpio_reset) {
			gpio_direction_output(gpio_reset, 0);
			mdelay(500);
			gpio_direction_output(gpio_reset, 1);
		}
	} else {
		gpio_direction_output(gpio_power_down, 0);
		if(gpio_reset)
			gpio_direction_output(gpio_reset, 0);
	}

	if (pdata->detect_irq && pdata->mmc_data)
		pdata->detect_irq(-1, pdata->mmc_data);

	gpio_free(gpio_power_down);
	if(gpio_reset)
		gpio_free(gpio_reset);
	return 0;
}

static int sd8x_rfkill_set(void *data, enum rfkill_state state)
{
	struct rfkill *dev = NULL, *another_dev = NULL;
	int ret = 0;
	struct sd8x_rfkill_data *sd8x_data =
		(struct sd8x_rfkill_data*)data;

	if (!sd8x_data->pdata->wlan_rfkill ||
		!sd8x_data->pdata->bt_rfkill) //in probing, just return
		return 0;

	if (sd8x_data->type == RFKILL_TYPE_WLAN) {
		dev = sd8x_data->pdata->wlan_rfkill;
		another_dev = sd8x_data->pdata->bt_rfkill;
	} else if (sd8x_data->type == RFKILL_TYPE_BLUETOOTH) {
		dev = sd8x_data->pdata->bt_rfkill;
		another_dev = sd8x_data->pdata->wlan_rfkill;
	}

	pr_debug("%s: try to set state of type(%d) as %d\n",
		__FUNCTION__, sd8x_data->type, state);
	if (another_dev->state != RFKILL_STATE_UNBLOCKED
		&& dev->state != RFKILL_STATE_UNBLOCKED
		&& state == RFKILL_STATE_UNBLOCKED)
		ret = sd8x_power_on(sd8x_data->pdata, 1);
	else if (another_dev->state != RFKILL_STATE_UNBLOCKED
		&& state != RFKILL_STATE_UNBLOCKED)
		ret = sd8x_power_on(sd8x_data->pdata, 0);

	return ret;
}

static struct rfkill * sd8x_rfkill_register(struct device *parent,
	enum rfkill_type type, char *name)
{
	int err;
	struct rfkill *dev;
	struct sd8x_rfkill_data *sd8x_data;

	dev = rfkill_allocate(parent, type);
	if (!dev)
		return NULL;
	sd8x_data = kzalloc(sizeof(struct sd8x_rfkill_data), GFP_KERNEL);
	if (!sd8x_data)
		return NULL;
	sd8x_data->type = type;
	sd8x_data->pdata = (struct sd8x_rfkill_platform_data *)parent->platform_data;

	dev->name = name;
	dev->data = sd8x_data;
	dev->state = RFKILL_STATE_SOFT_BLOCKED;
	dev->toggle_radio = sd8x_rfkill_set;
	dev->user_claim_unsupported = 1;

	err = rfkill_register(dev);
	if (err) {
		rfkill_free(dev);
		return ERR_PTR(err);
	}

	return dev;
}

static int sd8x_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *rfkill = NULL;
	struct sd8x_rfkill_platform_data *pdata =
		pdev->dev.platform_data;

	sd8x_power_on(pdata, 0);
	rfkill_set_default(RFKILL_TYPE_WLAN, RFKILL_STATE_SOFT_BLOCKED);
	rfkill_set_default(RFKILL_TYPE_BLUETOOTH, RFKILL_STATE_SOFT_BLOCKED);


	rfkill = sd8x_rfkill_register(&pdev->dev,
		RFKILL_TYPE_WLAN, "sd8xxx-wlan");
	if (IS_ERR(rfkill))
		return PTR_ERR(rfkill);
	pdata->wlan_rfkill = rfkill;

	rfkill = sd8x_rfkill_register(&pdev->dev,
		RFKILL_TYPE_BLUETOOTH, "sd8xxx-bluetooth");
	if (IS_ERR(rfkill)) {
		rfkill_unregister(pdata->wlan_rfkill);
		return PTR_ERR(rfkill);
	}
	pdata->bt_rfkill = rfkill;
	pr_debug("wlan_rfkill=%p, bt_rfkill=%p\n",
		pdata->wlan_rfkill, pdata->bt_rfkill);

	return 0;
}

static int sd8x_rfkill_remove(struct platform_device *pdev)
{
	struct sd8x_rfkill_platform_data *pdata =
		pdev->dev.platform_data;

	rfkill_unregister(pdata->wlan_rfkill);
	rfkill_free(pdata->wlan_rfkill);
	rfkill_unregister(pdata->bt_rfkill);
	rfkill_free(pdata->bt_rfkill);

	return 0;
}

static int sd8x_rfkill_suspend(struct platform_device *pdev, pm_message_t pm_state)
{
	struct sd8x_rfkill_platform_data *pdata =
		pdev->dev.platform_data;

	/* For WIFI module is totally powered off, we need marked state
	 * as blocked so as to trigger re-download firmware
	 * when rfkill framework restore original active status */
	if(PM_EVENT_SUSPEND == pm_state.event){
		pdata->wlan_rfkill->state = RFKILL_STATE_SOFT_BLOCKED;
	}

	return 0;
}

static int sd8x_rfkill_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sd8x_rfkill_platform_driver = {
	.probe = sd8x_rfkill_probe,
	.remove = sd8x_rfkill_remove,
	.driver = {
		.name = SD8X_DEV_NAME,
		.owner = THIS_MODULE,
	},
	.suspend = sd8x_rfkill_suspend,
	.resume = sd8x_rfkill_resume,
};

static int __init sd8x_rfkill_init(void)
{
	return platform_driver_register(&sd8x_rfkill_platform_driver);
}

static void __exit sd8x_rfkill_exit(void)
{
	platform_driver_unregister(&sd8x_rfkill_platform_driver);
}

module_init(sd8x_rfkill_init);
module_exit(sd8x_rfkill_exit);

MODULE_ALIAS("platform:sd8x_rfkill");
MODULE_DESCRIPTION("sd8x_rfkill");
MODULE_AUTHOR("Marvell");
MODULE_LICENSE("GPL");

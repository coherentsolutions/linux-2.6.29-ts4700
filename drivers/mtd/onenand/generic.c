/*
 *  linux/drivers/mtd/onenand/generic.c
 *
 *  Copyright (c) 2005 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the OneNAND flash for generic boards.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <mach/pxa3xx_bbm.h>

#include <asm/io.h>
#include <asm/mach/flash.h>

#define DRIVER_NAME	"onenand"


#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL,  };
#endif

struct onenand_info {
	struct mtd_info		mtd;
	struct mtd_partition	*parts;
	struct onenand_chip	onenand;
};

static int __devinit generic_onenand_probe(struct platform_device *pdev)
{
	struct onenand_info *info;
	struct flash_platform_data *pdata = pdev->dev.platform_data;
	struct resource *res = pdev->resource;
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL, *parts = NULL;
	int part_num;
#endif
	unsigned long size = res->end - res->start + 1;
	int err;

	info = kzalloc(sizeof(struct onenand_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!request_mem_region(res->start, size, pdev->name)) {
		err = -EBUSY;
		goto out_free_info;
	}

	info->onenand.base = ioremap(res->start, size);
	if (!info->onenand.base) {
		err = -ENOMEM;
		goto out_release_mem_region;
	}

	info->onenand.mmcontrol = pdata->mmcontrol;
	info->onenand.irq = platform_get_irq(pdev, 0);

	info->mtd.name = dev_name(&pdev->dev);
	info->mtd.priv = &info->onenand;
	info->mtd.owner = THIS_MODULE;

#ifdef CONFIG_PXA3XX_BBM
	info->onenand.scan_bbt = pxa3xx_scan_bbt;
	info->onenand.block_bad = pxa3xx_block_bad;
	info->onenand.block_markbad = pxa3xx_block_markbad;
#else
	info->onenand.scan_bbt = NULL;
	info->onenand.block_bad = NULL;
	info->onenand.block_markbad = NULL;
#endif

	if (onenand_scan(&info->mtd, 1)) {
		err = -ENXIO;
		goto out_iounmap;
	}

#ifdef CONFIG_MTD_PARTITIONS
	err = parse_mtd_partitions(&info->mtd, part_probes, &info->parts, 0);
	if (err > 0) {
		partitions = info->parts;
		part_num = err;
	}
	else if (err <= 0 && pdata->parts) {
		partitions = pdata->parts;
		part_num = pdata->nr_parts;
	}

	if (part_num > 0 && partitions) {
#ifdef CONFIG_PXA3XX_BBM
			struct pxa3xx_bbm *pxa3xx_bbm = info->mtd.bbm;
			parts = pxa3xx_bbm->check_partition(&info->mtd, partitions, &part_num);
			if (!parts)
				return -EINVAL;
#else
			parts = partitions;
#endif
		add_mtd_partitions(&info->mtd, parts, part_num);
#ifdef CONFIG_PXA3XX_BBM
		kfree(parts);
#endif
	}
	else
#endif
		err = add_mtd_device(&info->mtd);

	dev_set_drvdata(&pdev->dev, info);

	return 0;

out_iounmap:
	iounmap(info->onenand.base);
out_release_mem_region:
	release_mem_region(res->start, size);
out_free_info:
	kfree(info);

	return err;
}

static int __devexit generic_onenand_remove(struct platform_device *pdev)
{
	struct onenand_info *info = dev_get_drvdata(&pdev->dev);
	struct resource *res = pdev->resource;
	unsigned long size = res->end - res->start + 1;

	dev_set_drvdata(&pdev->dev, NULL);

	if (info) {
		if (info->parts)
			del_mtd_partitions(&info->mtd);
		else
			del_mtd_device(&info->mtd);

		onenand_release(&info->mtd);
		release_mem_region(res->start, size);
		iounmap(info->onenand.base);
		kfree(info);
	}

	return 0;
}

static struct platform_driver generic_onenand_driver = {
	.driver = {
		.name	= DRIVER_NAME,
	},
	.probe		= generic_onenand_probe,
	.remove		= __devexit_p(generic_onenand_remove),
#ifdef CONFIG_PM
	.suspend	= NULL,
	.resume		= NULL,
#endif
};
MODULE_ALIAS(DRIVER_NAME);

static int __init generic_onenand_init(void)
{
	return platform_driver_register(&generic_onenand_driver);
}

static void __exit generic_onenand_exit(void)
{
	platform_driver_unregister(&generic_onenand_driver);
}

module_init(generic_onenand_init);
module_exit(generic_onenand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on generic boards");

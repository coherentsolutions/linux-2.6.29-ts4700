/*
 * PXA168 DVFM Driver
 *
 * Copyright (C) 2008 Marvell Corporation
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html
 *
 * (C) Copyright 2007 Marvell International Ltd.
 * All Rights Reserved
 */

#undef DEBUG
#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/err.h>

#include <asm/io.h>

#include <mach/cputype.h>
#include <mach/hardware.h>
#include <mach/dvfm.h>
#include <mach/pxa168_dvfm.h>

#include <plat/pxa3xx_pmic.h>
#include <mach/pxa168_pm.h>
#include <asm/cacheflush.h>
#include <mach/mspm_prof.h>

#include <mach/regs-mpmu.h>
#include <mach/regs-apmu.h>

#define CONFIG_PXA910_DVFM_ASYNC_MODE 1
#define CONFIG_PXA910_AP_ALONE_MODE 1

unsigned long dfc_lock_cache_code_base;
EXPORT_SYMBOL(dfc_lock_cache_code_base);


static struct info_head pxa168_dvfm_op_list = {
	.list = LIST_HEAD_INIT(pxa168_dvfm_op_list.list),
	.lock = RW_LOCK_UNLOCKED,
};

/* the operating point preferred by policy maker or user */
static int preferred_op;


static int dvfm_dev_id;

static struct pxa168_opmode_md pxa168_opmode_md_array[] = {
	/* pclk, dclk, xpclk, baclk, aclk, aclk2, vcc_core, pll, mode */
	/* 156,	156, 156, 156, 156, 156,  900, 312  0 */
	{
		.name = "mode 0",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 0,
		.axipll_sel = 0,
		.aclk2_div = 1,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 0,
		.baclk_div = 0,
		.pclk_div = 1,
		.pll2_refdiv = 0,
		.pll2_fbdiv = 0,
		.pll2_reg1 = 0x00000000,
		.vcc_core = 900,
	},
	/* 400, 200, 200, 200, 156, 156,  900, 0, 1, 0, 404, 1 */
	{
		.name = "mode 1",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 1,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 11,
		.pll2_fbdiv = 200,
		.pll2_reg1 = 0x90000164,
		.vcc_core = 900,
	},
	/* 624, 312, 312, 156, 156, 156, 1000, 624  2 */
	{
		.name = "mode 2",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 1,
		.axipll_sel = 0,
		.aclk2_div = 1,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 0,
		.pll2_fbdiv = 0,
		.pll2_reg1 = 0x00000000,
		.vcc_core = 1000,
	},
	/* 624, 208, 312, 156, 156, 156, 1000, 4, 6, 0, 1248 2_1 */
	{
		.name = "mode 2.1",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 7,
		.aclk_div = 7,
		.dclk_div = 2,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 1,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 142,
		.pll2_reg1 = 0x90040664,
		.vcc_core = 1000,
	},
	/* 624, 312, 312, 156, 156, 156, 1000, 624,  2_2 */
	{
		.name = "mode 2.2",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 1,
		.axipll_sel = 0,
		.aclk2_div = 1,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 0,
		.pll2_fbdiv = 0,
		.pll2_reg1 = 0x00000000,
		.vcc_core = 1000,
	},
	/* 624, 156, 312, 156, 156, 156, 1000, 624, 2_3 */
	{
		.name = "mode 2.3",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 1,
		.axipll_sel = 1,
		.aclk2_div = 3,
		.aclk_div = 3,
		.dclk_div = 1,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 0,
		.pll2_fbdiv = 0,
		.pll2_reg1 = 0x00000000,
		.vcc_core = 1000,
	},
	/* 800, 400, 400, 200, 156, 312, 1000, 2, 3, 0, 806, 3 */
	{
		.name = "mode 3",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 91,
		.pll2_reg1 = 0x90020364,
		.vcc_core = 1000,
	},
	/* 800, 200, 400, 200, 156, 312, 1000, 2, 3, 0, 806, 3_1 */
	{
		.name = "mode 3.1",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 1,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 91,
		.pll2_reg1 = 0x90020364,
		.vcc_core = 1000,
	},
	/* 800, 400, 400, 200, 156, 312, 1000, 2, 3, 0, 806, 3_2 */
	{
		.name = "mode 3.2",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 91,
		.pll2_reg1 = 0x90020364,
		.vcc_core = 1000,
	},
	/* 800, 200, 400, 200, 156, 312, 1000, 2, 3, 0, 806, 3_3 */
	{
		.name = "mode 3.3",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 1,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 91,
		.pll2_reg1 = 0x90020364,
		.vcc_core = 1000,
	},

	/* 1066, 533, 533, 265, 156, 312, 1100, 4, 6, 0, 1083, 4 */
	{
		.name = "mode 4",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 1,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 123,
		.pll2_reg1 = 0x90040664,
		.vcc_core = 1100,
	},
	/* 1200, 400, 400, 300, 156, 312, 1100, 5, 7, 1, 1213, 5 */
	/*{
		.name = "mode 5",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 2,
		.baclk_div = 3,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 138,
		.pll2_reg1 = 0x900d0764,
		.vcc_core = 1100,
	},*/
	/* 1600, 533, 533, 320, 156, 312, 1150, 5, 7, 1, 1612, 6 */
	/*{
		.name = "mode 6",
		.power_mode = POWER_MODE_ACTIVE,
		.corepll_sel = 2,
		.axipll_sel = 0,
		.aclk2_div = 0,
		.aclk_div = 1,
		.dclk_div = 0,
		.xpclk_div = 3,
		.baclk_div = 5,
		.pclk_div = 0,
		.pll2_refdiv = 1,
		.pll2_fbdiv = 184,
		.pll2_reg1 = 0x900d0764,
		.vcc_core = 1150,
	},*/
	/* core internal idle */
	{
		.power_mode = POWER_MODE_CORE_INTIDLE,
		.name = "core_intidle",
	},
	/* core external idle */
	{
		.power_mode = POWER_MODE_CORE_EXTIDLE,
		.name = "core_extidle",
	},
	/* application subsystem idle */
	{
		.power_mode = POWER_MODE_APPS_IDLE,
		.name = "apps_idle",
	},
	/* application subsystem sleep */
	{
		.power_mode = POWER_MODE_APPS_SLEEP,
		.name = "apps_sleep",
	},
	/* system sleep */
	{
		.power_mode = POWER_MODE_SYS_SLEEP,
		.name = "sys_sleep",
	},
	/* hibernate */
	{
		.power_mode = POWER_MODE_HIBERNATE,
		.name = "hibernate",
	},


};

static int aspen_vcc_core_convt[][3] = {
	/* cnt,  in,   out */
	{ 0x00,  725,  379 },
	{ 0x01,  750,  408 },
	{ 0x02,  775,  436 },
	{ 0x03,  800,  464 },
	{ 0x04,  825,  493 },
	{ 0x05,  850,  521 },
	{ 0x06,  875,  550 },
	{ 0x07,  900,  578 },
	{ 0x08,  925,  606 },
	{ 0x09,  950,  635 },
	{ 0x0A,  975,  663 },
	{ 0x0B, 1000,  691 },
	{ 0x0C, 1025,  720 },
	{ 0x0D, 1050,  748 },
	{ 0x0E, 1075,  776 },
	{ 0x0F, 1100,  805 },
	{ 0x10, 1125,  833 },
	{ 0x11, 1150,  861 },
	{ 0x12, 1175,  890 },
	{ 0x13, 1200,  918 },
	{ 0x14, 1225,  947 },
	{ 0x15, 1250,  975 },
	{ 0x16, 1275, 1003 },
	{ 0x17, 1300, 1032 },
	{ 0x18, 1325, 1060 },
	{ 0x19, 1350, 1088 },
	{ 0x1A, 1375, 1117 },
	{ 0x1B, 1400, 1145 },
	{ 0x1C, 1425, 1173 },
	{ 0x1D, 1450, 1202 },
	{ 0x1E, 1475, 1230 },
	{ 0x1F, 1500, 1258 },
	{ 0x20, 1525, 1287 },
	{ 0x21, 1550, 1315 },
	{ 0x22, 1575, 1343 },
	{ 0x23, 1600, 1372 },
	{ 0x24, 1625, 1400 },
	{ 0x25, 1650, 1429 },
	{ 0x26, 1675, 1457 },
	{ 0x27, 1700, 1485 },
	{ 0x28, 1725, 1514 },
	{ 0x29, 1750, 1542 },
	{ 0x2A, 1775, 1570 },
	{ 0x2B, 1800, 1599 },
};

#define ASPEN_ECO11_SIZE (sizeof(aspen_vcc_core_convt) / \
		sizeof(aspen_vcc_core_convt[0]))

/* #####################Debug Function######################## */
static int dump_op(void *driver_data, struct op_info *p, char *buf)
{
	int len, count, x, i, max, sum;
	struct pxa168_md_opt *q = (struct pxa168_md_opt *)p->op;
	/*struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt md_opt_temp;
	struct pxa168_opmode_md opmode_md_temp;*/

	if (q == NULL)
		len = sprintf(buf, "Can't dump the op info\n");
	else {
		/* calculate how much bits is set in device word */
		max = DVFM_MAX_CLIENT >> 5;
		for (i = 0, sum = 0; i < max; i++) {
			x = p->device[i];
			for (count = 0; x; x = x & (x - 1), count++);
			sum += count;
		}
		len = sprintf(buf, "OP:%d name:%s [%s, %d]\n",
				p->index, q->name, (sum)?"Disabled"
				:"Enabled", sum);
		if (q->power_mode == POWER_MODE_ACTIVE)
			len += sprintf(buf + len, "pclk:%d baclk:%d xpclk:%d "
				"dclk:%d aclk:%d aclk2:%d vcc_core:%d\n",
				q->pclk, q->baclk, q->xpclk,
				q->dclk, q->aclk, q->aclk2, q->vcc_core);
	}

	/* pxa168_get_current_opmode_md(info, &opmode_md_temp);
	pxa168_op_machine_to_human(&opmode_md_temp, &md_opt_temp);
	len += sprintf(buf + len, "pclk:%d baclk:%d xpclk:%d "
				"dclk:%d aclk:%d aclk2:%d vcc_core:%d\n",
				md_opt_temp.pclk, md_opt_temp.baclk,
				md_opt_temp.xpclk, md_opt_temp.dclk,
				md_opt_temp.aclk, md_opt_temp.aclk2,
				md_opt_temp.vcc_core);*/

	return len;
}

static int dump_op_list(void *driver_data, struct info_head *op_table)
{
	struct op_info *p = NULL;
	struct list_head *list = NULL;
	struct pxa168_dvfm_info *info = driver_data;
	char buf[256];

	if (!op_table || list_empty(&op_table->list)) {
		printk(KERN_WARNING "op list is null\n");
		return -EINVAL;
	}
	memset(buf, 0, 256);
	list_for_each(list, &op_table->list) {
		p = list_entry(list, struct op_info, list);
		dump_op(info, p, buf);
	}
	return 0;
}

void pxa168_op_machine_to_human(struct pxa168_opmode_md  *opmode_md,
					struct pxa168_md_opt *opmode_hu)
{
	unsigned int corepll, axipll;
	int nume_temp, deno_temp;
	int post_div;
	int post_div_ration_nume = 1;
	int post_div_ration_deno = 1;
	opmode_hu->power_mode = opmode_md->power_mode;
	strncpy(opmode_hu->name, opmode_md->name, OP_NAME_LEN);
	if (opmode_md->power_mode == POWER_MODE_ACTIVE) {
		if (opmode_md->corepll_sel == 0) {
			corepll = 312;
		} else if (opmode_md->corepll_sel == 1) {
			corepll = 624;
		} else {
			nume_temp = opmode_md->pll2_fbdiv;
			deno_temp = opmode_md->pll2_refdiv;
			switch (deno_temp) {
			case 0x00:
				deno_temp = 1;
				break;
			case 0x10:
				deno_temp = 2;
				break;
			case 0x01:
				deno_temp = 3;
				break;
			default:
				deno_temp += 2;
				break;
			}
			corepll = 26*nume_temp/deno_temp;
		}

		if (opmode_md->axipll_sel == 0) {
			axipll = 312;
		} else if (opmode_md->axipll_sel == 1) {
			axipll = 624;
		} else {
			nume_temp = opmode_md->pll2_fbdiv;
			deno_temp = opmode_md->pll2_refdiv;
			switch (deno_temp) {
			case 0x00:
				deno_temp = 1;
				break;
			case 0x10:
				deno_temp = 2;
				break;
			case 0x01:
				deno_temp = 3;
				break;
			default:
				deno_temp += 2;
				break;
			}
			axipll = 26*nume_temp/deno_temp;
		}

		if (opmode_md->corepll_sel == 2) {

			post_div = (opmode_md->pll2_reg1 &
				MPMU_PLL2_REG1_VCODIV_SEL_DIFF_MSK)>>
				MPMU_PLL2_REG1_VCODIV_SEL_DIFF_BASE;

			switch (post_div) {
			case 0x0:
				post_div_ration_nume = 1;
				break;
			case 0x1:
				post_div_ration_nume = 3;
				post_div_ration_deno = 2;
				break;
			case 0x2:
				post_div_ration_nume = 2;
				break;
			case 0x3:
				post_div_ration_nume = 5;
				post_div_ration_deno = 2;
				break;
			case 0x4:
				post_div_ration_nume = 3;
				break;
			case 0x5:
				post_div_ration_nume = 4;
				break;
			case 0x6:
				post_div_ration_nume = 8;
				break;
			case 0x7:
				post_div_ration_nume = 6;
				break;
			case 0x9:
				post_div_ration_nume = 10;
				break;
			case 0xa:
				post_div_ration_nume = 12;
				break;
			case 0xb:
				post_div_ration_nume = 14;
				break;
			}
		}

		opmode_hu->pclk = corepll/(opmode_md->pclk_div + 1);
		opmode_hu->baclk = corepll/(opmode_md->baclk_div + 1);
		opmode_hu->xpclk = corepll/(opmode_md->xpclk_div + 1);
		opmode_hu->dclk = (corepll*post_div_ration_deno)/
			(2*(opmode_md->dclk_div + 1)*post_div_ration_nume);
		opmode_hu->aclk = axipll/(opmode_md->aclk_div + 1);
		opmode_hu->aclk2 = axipll/(opmode_md->aclk2_div + 1);
		opmode_hu->vcc_core = opmode_md->vcc_core;
		opmode_hu->lpj = opmode_hu->pclk * 5000;
	}
	return;
}

uint32_t pxa168_enable_swdfc(struct pxa168_dvfm_info *driver_data)
{
	uint32_t mpmu_acgr;
	uint32_t apmu_ccr;
	struct pxa168_dvfm_info *info = driver_data;

	/* some clocks are needed to allow DFC. Ensure they are enabled. */
	mpmu_acgr = readl(info->pmum_base + MPMU_ACGR_OFF);
	mpmu_acgr |= (0u<<20) |	/* 52MHz for APB2 (1u select APB2@26MHz) */
		(1u<<15) |	/* 624MHz */
		(1u<<14) |	/* PLL2 */
		(1u<<13) |	/* 312MHz */
		(1u<<9) |	/* GPC */
		(1u<<4); 	/* 26Mhz to APB */
	writel(mpmu_acgr, info->pmum_base + MPMU_ACGR_OFF);
	pr_debug(">>>>>%s: mpmu_acgr = %x\n", __func__,  mpmu_acgr);
	/****************************************************************/
	/* 	SWDFC Enable/Disabe: this is a new feature on A0.	*/
	/* 	Bit 21 is an nEnable to allow SW to initiate DFC.	*/
	/*	Must make sure the bit is clear, so SW can initiate DFC.*/
	/****************************************************************/
	apmu_ccr  = readl(info->pmua_base + APMU_CCR_OFF);
	apmu_ccr &= ~(1u<<21);
	apmu_ccr &= ~(0xffu<<24);
	writel(apmu_ccr, info->pmua_base + APMU_CCR_OFF);
	pr_debug(">>>>>%s: apmu_ccr = %x\n", __func__,  apmu_ccr);

	return apmu_ccr;
}

uint32_t pxa168_dfc_prepare(struct pxa168_dvfm_info *driver_data)
{
	uint32_t apmu_ccr;
	uint32_t apmu_imr;
	uint32_t apmu_isr;
	uint32_t apmu_temp;
	struct pxa168_dvfm_info *info = driver_data;

	/*  enable notification of dynamic frequency change events */
	apmu_imr = readl(info->pmua_base + APMU_IMR_OFF);
	/* enabling DFC done notification for pclk, dclk and aclk */
	apmu_imr |= 0x3a;
	writel(apmu_imr, info->pmua_base + APMU_IMR_OFF);
	pr_debug(">>>>>%s: apmu_imr = %x\n", __func__,  apmu_imr);

	/*  clear out any status bits left over from previous events. */
	apmu_isr = readl(info->pmua_base + APMU_ISR_OFF);
	writel(apmu_isr, info->pmua_base + APMU_ISR_OFF);
	pr_debug(">>>>>%s: apmu_isr = %x\n", __func__,  apmu_isr);

	/* clear the initiate bits during this stage. */
	apmu_ccr = readl(info->pmua_base + APMU_CCR_OFF);
	apmu_ccr &= ~(0xffu<<24);
	writel(apmu_ccr, info->pmua_base + APMU_CCR_OFF);
	pr_debug(">>>>>%s: apmu_ccr = %x\n", __func__,  apmu_ccr);

	apmu_ccr = readl(info->pmua_base + APMU_CCR_OFF);
	apmu_ccr &= ~(0xffu<<24);
	apmu_ccr |= (0xfu<<24);

	apmu_temp = readl(info->pmua_base);
	if(cpu_is_pxa168_A0()==0)
		apmu_temp |= 0x28000000;
	else
	{
		/* these two bits (31&27) should be set in reg address 0xd4282800 */
		/* otherwise, no ir. ??  needed to check with DE */
		apmu_temp |= 0x88000000;
	}
	writel(apmu_temp, info->pmua_base);
	pr_debug(">>>>>%s: PHY(%x) = %x\n", __func__,\
			0xd4282800, apmu_temp);

	/* make sure the allow sw mc control bit is not set */
	writel(0x00000302, info->pmua_base + APMU_IDLE_CFG_OFF );

	return apmu_ccr;
}

void pxa168_set_opmode_md(struct pxa168_dvfm_info *driver_data,
		struct pxa168_opmode_md *opmode_md)
{
	uint32_t apmu_ccr, mpmu_fccr, mpmu_pll2cr, mpmu_pll2_reg1,
		 mpmu_pll2_reg2;
	struct pxa168_dvfm_info *info = driver_data;

	/* if this is a pll2 mode, set up the refdiv,
	 *  fbdiv set, kvco, vrng and post dividers.*/

	if (opmode_md->corepll_sel == 2) {
		/* first must allow software to control pll2 activation */
		mpmu_pll2cr = readl(info->pmum_base + MPMU_PLL2CR_OFF);
		mpmu_pll2cr |=  (1u<<9);
		writel(mpmu_pll2cr, info->pmum_base + MPMU_PLL2CR_OFF);
		pr_debug(">>>>>%s: mpmu_pll2cr = %x\n", __func__, \
			mpmu_pll2cr);

		/* clear the pll2 enable bit to disable pll2 */
		mpmu_pll2cr &= ~(1u<<8);
		writel(mpmu_pll2cr, info->pmum_base + MPMU_PLL2CR_OFF);
		pr_debug(">>>>>%s: mpmu_pll2cr = %x\n", __func__, \
			mpmu_pll2cr);

		/* set the new pll2 frequencies. */
		mpmu_pll2cr = readl(info->pmum_base + MPMU_PLL2CR_OFF);
		mpmu_pll2cr &= ~((0x1f<<19) | (0x1ff<<10));
		mpmu_pll2cr |=  ((opmode_md->pll2_refdiv<<19) | \
				(opmode_md->pll2_fbdiv<<10));
		writel(mpmu_pll2cr, info->pmum_base + MPMU_PLL2CR_OFF);
		pr_debug(">>>>>%s: mpmu_pll2cr = %x\n", __func__,  \
				mpmu_pll2cr);

		/* set up the kvco, vrng and post divider values. */
		mpmu_pll2_reg1 = opmode_md->pll2_reg1;
		writel(mpmu_pll2_reg1, info->pmum_base + MPMU_PLL2_REG1_OFF);
		pr_debug(">>>>>%s: mpmu_pll2_reg1 = %x\n", __func__, \
				mpmu_pll2_reg1);

		/* ensure differential clock mode is set when using pll2 */
		mpmu_pll2_reg2 = readl(info->pmum_base + MPMU_PLL2_REG2_OFF);
		mpmu_pll2_reg2 |= (1u<<6);
		writel(mpmu_pll2_reg2, info->pmum_base + MPMU_PLL2_REG2_OFF);
		pr_debug(">>>>>%s: mpmu_pll2_reg2 = %x\n", __func__,  \
				mpmu_pll2_reg2);

		/* enable pll2 */
		mpmu_pll2cr = readl(info->pmum_base + MPMU_PLL2CR_OFF);
		mpmu_pll2cr |=  (1u<<8);
		writel(mpmu_pll2cr, info->pmum_base + MPMU_PLL2CR_OFF);
		pr_debug(">>>>>%s: mpmu_pll2cr = %x\n", __func__, \
				mpmu_pll2cr);

	}

	/* select the PLL sources, including for core/ddr/axi */
	mpmu_fccr = readl(info->pmum_base + MPMU_FCCR_OFF);
	mpmu_fccr &= ~(7u<<29);
	mpmu_fccr |=  (opmode_md->corepll_sel<<29);
	mpmu_fccr &= ~(7u<<23);
	mpmu_fccr |=  (opmode_md->axipll_sel<<23);
	mpmu_fccr |= 0x0000888e;
	writel(mpmu_fccr, info->pmum_base + MPMU_FCCR_OFF);
	pr_debug(">>>>>%s: mpmu_fccr = %x\n", __func__,  mpmu_fccr);

	/* select the divider for each clock */
	apmu_ccr  = readl(info->pmua_base + APMU_CCR_OFF);
	apmu_ccr &= 0xFFF00000;
	apmu_ccr |= (opmode_md->aclk2_div<<18) |
				(opmode_md->aclk_div<<15) |
				(opmode_md->dclk_div<<12) |
				(opmode_md->xpclk_div<<9) |
				(opmode_md->baclk_div<<6) |
				(opmode_md->pclk_div<<0);
	writel(apmu_ccr, info->pmua_base + APMU_CCR_OFF);
	pr_debug(">>>>>%s: apmu_ccr = %x\n", __func__,  apmu_ccr);
	return;

}

static int get_op_num(void *driver_data, struct info_head *op_table)
{
	struct list_head *entry = NULL;
	int num = 0;

	if (!op_table)
		goto out;
	read_lock(&op_table->lock);
	if (list_empty(&op_table->list)) {
		read_unlock(&op_table->lock);
		goto out;
	}
	list_for_each(entry, &op_table->list) {
		num++;
	}
	read_unlock(&op_table->lock);
out:
	return num;
}

static char *get_op_name(void *driver_data, struct op_info *p)
{
	struct pxa168_md_opt *q = NULL;
	if (p == NULL)
		return NULL;
	q = (struct pxa168_md_opt *)p->op;
	return q->name;
}

static void update_voltage(int vcc_core)
{
	int pmic_vcc_core = aspen_vcc_core_convt[0][1];
	int i;

	for (i = 0; i < ASPEN_ECO11_SIZE-1; i++) {
		if ((aspen_vcc_core_convt[i][2] < vcc_core) && \
				(aspen_vcc_core_convt[i+1][2] > vcc_core))
			pmic_vcc_core = aspen_vcc_core_convt[i+1][1];
	}
	pr_debug("vcc_core: %d, pmic setting = %d\n", vcc_core, pmic_vcc_core);
	pxa3xx_pmic_set_voltage(VCC_CORE, pmic_vcc_core);
}

static int set_freq(void *driver_data, int old_index, int new_index)
{
	struct pxa168_dvfm_info *info = driver_data;
	uint32_t apmu_ccr;

	pr_debug("old_index = %d, new_index = %d\n", old_index, new_index);
	/* set up the core voltage */
	if (pxa168_opmode_md_array[new_index].vcc_core > \
			pxa168_opmode_md_array[old_index].vcc_core)
		update_voltage(pxa168_opmode_md_array[new_index].vcc_core);
	/* launch dfc */
	pxa168_enable_swdfc(info);
	pxa168_set_opmode_md(info, &(pxa168_opmode_md_array[new_index]));
	apmu_ccr = pxa168_dfc_prepare(info);
	pr_debug(">>>>>%s: apmu_ccr = %x\n", __func__,  apmu_ccr);
	__cpuc_flush_kern_all();
	pxa168_trigger_dfc(apmu_ccr);

	/* set up the core voltage */
	if (pxa168_opmode_md_array[new_index].vcc_core < \
			pxa168_opmode_md_array[old_index].vcc_core)
		update_voltage(pxa168_opmode_md_array[new_index].vcc_core);

	return 0;
}

static int update_freq(void *driver_data, struct dvfm_freqs *freqs)
{
	struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt *old = NULL, *new = NULL;
	struct op_info *p = NULL;
	unsigned long flags;
	int found = 0, new_op = cur_op;

	write_lock_irqsave(&pxa168_dvfm_op_list.lock, flags);
	if (!list_empty(&pxa168_dvfm_op_list.list)) {
		list_for_each_entry(p, &pxa168_dvfm_op_list.list, list) {
			if (p->index == freqs->old) {
				found++;
				old = (struct pxa168_md_opt *)p->op;
			}
			if (p->index == freqs->new) {
				found++;
				new = (struct pxa168_md_opt *)p->op;
				new_op = p->index;
			}
			if (found == 2)
				break;
		}
	}
	write_unlock_irqrestore(&pxa168_dvfm_op_list.lock, flags);
	if (found != 2)
		return -EINVAL;

	set_freq(info, freqs->old, new_op);
	cur_op = new_op;
	loops_per_jiffy = new->lpj;
	return 0;
}

static void do_freq_notify(void *driver_data, struct dvfm_freqs *freqs)
{
	struct pxa168_dvfm_info *info = driver_data;

	dvfm_notifier_frequency(freqs, DVFM_FREQ_PRECHANGE);
	update_freq(info, freqs);
	dvfm_notifier_frequency(freqs, DVFM_FREQ_POSTCHANGE);
}

static void do_lowpower_notify(void *driver_data, struct dvfm_freqs *freqs, \
		unsigned int state)
{
	dvfm_notifier_frequency(freqs, DVFM_FREQ_PRECHANGE);
	pxa168_pm_enter_lowpower_mode(state);
	dvfm_notifier_frequency(freqs, DVFM_FREQ_POSTCHANGE);

}

/* Check whether any client blocks the current operating point */
static int block_client(struct op_info *info)
{
	int i;
	unsigned int ret = 0;
	for (i = 0; i < (DVFM_MAX_CLIENT >> 5); i++)
		ret |= info->device[i];
	return (int)ret;
}

static int check_op(void *driver_data, struct dvfm_freqs *freqs,
		unsigned int new, unsigned int relation)
{
	struct op_info *p = NULL;
	int index, tmp_index = -1, found = 0;
	int first_op = 0;

	freqs->new = -1;
	if (!dvfm_find_op(new, &p)) {
		index = p->index;
	} else
		return -EINVAL;

	read_lock(&pxa168_dvfm_op_list.lock);
	if (relation == RELATION_LOW) {
		/* Set the lowest usable op that is higher than specifed one */
		/* Note: we assume bigger index number is more 'usable' */
		list_for_each_entry(p, &pxa168_dvfm_op_list.list, list) {
			if (!block_client(p) && (p->index >= index)) {
				if (tmp_index == -1 || (tmp_index >= p->index)) {
					if (first_op == 0)
						first_op = p->index;
					freqs->new = p->index;
					tmp_index = p->index;
					found = 1;
				}
				if (found && (new == p->index))
					break;
			}
		}
		if (found && (first_op == 1) && (new != p->index))
			freqs->new = first_op;
	} else if (relation == RELATION_HIGH) {
		/* Set the highest usable op that is lower than specified one */
		list_for_each_entry(p, &pxa168_dvfm_op_list.list, list) {
			if (!block_client(p) && (p->index <= index)) {
				if (tmp_index == -1 || tmp_index < p->index) {
					freqs->new = p->index;
					tmp_index = p->index;
				}
			}
		}
	} else if (relation == RELATION_STICK) {
		/* Set the specified frequency */
		list_for_each_entry(p, &pxa168_dvfm_op_list.list, list) {
			if (!block_client(p) && (p->index == new)) {
				freqs->new = p->index;
				break;
			}
		}
	}
	read_unlock(&pxa168_dvfm_op_list.lock);
	if (freqs->new == -1)
		return -EINVAL;
	return 0;
}

static int pxa168_set_op(void *driver_data, struct dvfm_freqs *freqs,
		unsigned int new, unsigned int relation)
{
	struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt *md = NULL;
	struct op_info *p = NULL;
	unsigned long flags;
	int ret;

	local_fiq_disable();
	local_irq_save(flags);

	ret = dvfm_find_op(freqs->old, &p);
	if (ret)
		goto out;

	memcpy(&freqs->old_info, p, sizeof(struct op_info));
	ret = check_op(info, freqs, new, relation);
	if (ret)
		goto out;

	if (!dvfm_find_op(freqs->new, &p)) {
		memcpy(&(freqs->new_info), p, sizeof(struct op_info));
		/* If find old op and new op is same, skip it.
		 * At here, ret should be zero.
		 */
		if (freqs->old_info.index == freqs->new_info.index)
			goto out;
		pr_debug("old op index = %d, new op index = %d\n", \
				freqs->old_info.index, freqs->new_info.index);
		md = (struct pxa168_md_opt *)p->op;
		switch (md->power_mode) {
		case POWER_MODE_ACTIVE:
			do_freq_notify(info, freqs);
			break;
		case POWER_MODE_CORE_INTIDLE:
		case POWER_MODE_CORE_EXTIDLE:
		case POWER_MODE_APPS_IDLE:
		case POWER_MODE_APPS_SLEEP:
		case POWER_MODE_SYS_SLEEP:
		case POWER_MODE_HIBERNATE:
			do_lowpower_notify(info, freqs, md->power_mode);
			break;
		}
	}

	local_irq_restore(flags);
	local_fiq_enable();
	return 0;
out:
	local_irq_restore(flags);
	local_fiq_enable();
	return ret;
}

static int pxa168_request_op(void *driver_data, int index)
{
	struct dvfm_freqs freqs;
	struct op_info *info = NULL;
	struct pxa168_md_opt *md = NULL;
	int relation, ret;

	ret = dvfm_find_op(index, &info);
	if (ret)
		goto out;
	freqs.old = cur_op;
	freqs.new = index;
	md = (struct pxa168_md_opt *)(info->op);
	relation = RELATION_LOW;
	ret = pxa168_set_op(driver_data, &freqs, index, relation);
	if (!ret)
		preferred_op = index;
out:
	return ret;
}

/*
 * The machine operation of dvfm_enable
 */
static int pxa168_enable_dvfm(void *driver_data, int dev_id)
{
	struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt *md = NULL;
	struct op_info *p = NULL;
	int i, num;
	num = get_op_num(info, &pxa168_dvfm_op_list);
	for (i = 0; i < num; i++) {
		if (!dvfm_find_op(i, &p)) {
			md = (struct pxa168_md_opt *)p->op;
			dvfm_enable_op(i, dev_id);
		}
	}
	return 0;
}

/*
 * The mach operation of dvfm_disable
 */
static int pxa168_disable_dvfm(void *driver_data, int dev_id)
{
	struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt *md = NULL;
	struct op_info *p = NULL;
	int i, num;
	num = get_op_num(info, &pxa168_dvfm_op_list);
	for (i = 0; i < num; i++) {
		if (!dvfm_find_op(i, &p)) {
			md = (struct pxa168_md_opt *)p->op;
			dvfm_disable_op(i, dev_id);
		}
	}
	return 0;
}

static int pxa168_enable_op(void *driver_data, int index, int relation)
{
	/*
	 * Restore preferred_op. Because this op is sugguested by policy maker
	 * or user.
	 */
	return pxa168_request_op(driver_data, preferred_op);
}

static int pxa168_disable_op(void *driver_data, int index, int relation)
{
	struct dvfm_freqs freqs;
	if (cur_op == index) {
		freqs.old = index;
		freqs.new = -1;
		dvfm_set_op(&freqs, freqs.old, relation);
	}
	return 0;
}


#ifdef CONFIG_MSPM_PXA168_STATS
/* Convert 32K ticks to microseconds */
static unsigned int ticks_32k_to_usec(unsigned int ticks)
{
	return (ticks * 5 * 5 * 5 * 5 * 5 * 5) >> 9;
}

static unsigned int ticks_32k_to_sec(unsigned int ticks)
{
	return ticks >> 15;
}


static unsigned int pxa168_read_32k_ticks(void)
{
	return read_timer()/100;
}
#else
#define pxa168_read_32k_ticks	NULL
#define ticks_32k_to_usec	NULL
#define ticks_32k_to_sec	NULL
#endif


static struct dvfm_driver pxa168_driver = {
	.count		= get_op_num,
	.set		= pxa168_set_op,
	.dump		= dump_op,
	.name		= get_op_name,
	.request_set	= pxa168_request_op,
	.enable_dvfm	= pxa168_enable_dvfm,
	.disable_dvfm	= pxa168_disable_dvfm,
	.enable_op	= pxa168_enable_op,
	.disable_op	= pxa168_disable_op,
	.ticks_to_usec  = ticks_32k_to_usec,
	.ticks_to_sec   = ticks_32k_to_sec,
	.read_time      = pxa168_read_32k_ticks,
};

void pxa168_get_current_opmode_md(struct pxa168_dvfm_info *driver_data,
		struct pxa168_opmode_md *opmode_md)
{
	uint32_t mpmu_fccr, apmu_ccsr, mpmu_posr, mpmu_pll2_reg1;
	struct pxa168_dvfm_info *info = driver_data;
	int pmic_vcc_core = aspen_vcc_core_convt[0][1];
	int i;

	mpmu_fccr = readl(info->pmum_base + MPMU_FCCR_OFF);
	mpmu_posr = readl(info->pmum_base + MPMU_POSR_OFF);
	mpmu_pll2_reg1 = readl(info->pmum_base + MPMU_PLL2_REG1_OFF);
	apmu_ccsr = readl(info->pmua_base + APMU_CCSR_OFF);

	opmode_md->corepll_sel = (mpmu_fccr & MPMU_FCCR_CORECLKSEL_MSK)>>
		MPMU_FCCR_CORECLKSEL_BASE;
	opmode_md->axipll_sel = (mpmu_fccr & MPMU_FCCR_AXICLKSEL_MSK)>>
		MPMU_FCCR_AXICLKSEL_BASE;
	opmode_md->aclk2_div = (apmu_ccsr & APMU_CCSR_BUS2_CLK_DIV_MSK)>>
		APMU_CCSR_BUS2_CLK_DIV_BASE;
	opmode_md->aclk_div = (apmu_ccsr & APMU_CCSR_BUS_CLK_DIV_MSK)>>
		APMU_CCSR_BUS_CLK_DIV_BASE;
	opmode_md->dclk_div = (apmu_ccsr & APMU_CCSR_DDR_CLK_DIV_MSK)>>
		APMU_CCSR_DDR_CLK_DIV_BASE;
	opmode_md->xpclk_div = (apmu_ccsr & APMU_CCSR_XP_CLK_DIV_MSK)>>
		APMU_CCSR_XP_CLK_DIV_BASE;
	opmode_md->baclk_div = (apmu_ccsr & APMU_CCSR_BIU_CLK_DIV_MSK)>>
		APMU_CCSR_BIU_CLK_DIV_BASE;
	opmode_md->pclk_div = (apmu_ccsr & APMU_CCSR_CORE_CLK_DIV_MSK)>>
		APMU_CCSR_CORE_CLK_DIV_BASE;
	opmode_md->pll2_refdiv = (mpmu_posr & MPMU_POSR_PLL2REFD_MSK)>>
		MPMU_POSR_PLL2REFD_BASE;
	opmode_md->pll2_fbdiv = (mpmu_posr & MPMU_POSR_PLL2FBD_MSK)>>
		MPMU_POSR_PLL2FBD_BASE;
	opmode_md->pll2_reg1 = mpmu_pll2_reg1;
	opmode_md->power_mode = POWER_MODE_ACTIVE;
	strncpy(opmode_md->name, "dump op" , OP_NAME_LEN);

	opmode_md->vcc_core = 0;
	pxa3xx_pmic_get_voltage(VCC_CORE, &pmic_vcc_core);
	for (i = 0; i < ASPEN_ECO11_SIZE; i++) {
		if ((aspen_vcc_core_convt[i][1] == pmic_vcc_core))
			opmode_md->vcc_core = aspen_vcc_core_convt[i][2];
	}
	return;
}

/* Produce a operating point table */
static int op_init(struct pxa168_dvfm_info *driver_data, struct info_head *op_table)
{
	struct pxa168_dvfm_info *info = driver_data;
	struct pxa168_md_opt *md, *smd;
	struct pxa168_opmode_md opmode_md_temp;
	unsigned long flags;
	int i, index;
	struct op_info *p = NULL, *q = NULL;

	write_lock_irqsave(&op_table->lock, flags);
	for (i = 0, index = 0; i < ARRAY_SIZE(pxa168_opmode_md_array); i++) {
		/* Set index of operating point used in idle */
		if (pxa168_opmode_md_array[i].power_mode != POWER_MODE_ACTIVE)
			set_idle_op(index,\
					pxa168_opmode_md_array[i].power_mode);
		p = (struct op_info *)kzalloc(sizeof(struct op_info),\
				GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		p->op = (struct pxa168_md_opt *)kzalloc\
				(sizeof(struct pxa168_md_opt), GFP_KERNEL);
		if (!(p->op))
			return -ENOMEM;
		pxa168_op_machine_to_human(&(pxa168_opmode_md_array[i]), \
			       p->op);
		p->index = index++;
		list_add_tail(&(p->list), &(op_table->list));
	}

	p = (struct op_info *)kzalloc(sizeof(struct op_info),
				GFP_KERNEL);
	if (!(p))
		return -ENOMEM;

	p->op = (struct pxa168_md_opt *)kzalloc\
				(sizeof(struct pxa168_md_opt), GFP_KERNEL);
	if (!(p->op))
		return -ENOMEM;
	md = (struct pxa168_md_opt *)p->op;
	/* capture the op info */
	pxa168_get_current_opmode_md(info, &opmode_md_temp);
	pxa168_op_machine_to_human(&opmode_md_temp, md);

	def_op = 0x5a5a;	/* magic number */
	list_for_each_entry(q, &(op_table->list), list) {
		smd = (struct pxa168_md_opt *)q->op;
		if (md->pclk == smd->pclk && \
				md->aclk2 == smd->aclk2 && \
				md->baclk == smd->baclk && \
				md->xpclk == smd->xpclk && \
				md->dclk == smd->dclk && \
				md->aclk == smd->aclk) {
			def_op = q->index;
			break;
		}
	}
	md->power_mode = POWER_MODE_ACTIVE;
	sprintf(md->name, "BOOT OP");

	q = (struct op_info *)kzalloc(sizeof(struct op_info), GFP_KERNEL);
	if (!(q))
		return -ENOMEM;
	q->op = (struct pxa168_md_opt *)kzalloc\
			(sizeof(struct pxa168_md_opt), GFP_KERNEL);
	if (!(q->op))
		return -ENOMEM;
	smd = (struct pxa168_md_opt *)q->op;
	memcpy(smd, md, sizeof(struct pxa168_md_opt));
	sprintf(smd->name, "CUSTOM OP");

	/* Add CUSTOM OP into op list */
	q->index = index++;
	list_add_tail(&q->list, &op_table->list);
	/* Add BOOT OP into op list */
	p->index = index++;
	list_add_tail(&p->list, &op_table->list);
	/* BOOT op */
	if (def_op == 0x5a5a) {
		cur_op = p->index;
		def_op = p->index;
	} else
		cur_op = def_op;
	preferred_op = cur_op;
	pr_debug("%s, def_op:%d, cur_op:%d\n", __func__, def_op, cur_op);

	/* set the operating point number */
	op_nums = ARRAY_SIZE(pxa168_opmode_md_array) + 2;

	printk("Current Operating Point is %d\n", cur_op);
	dump_op_list(info, op_table);
	write_unlock_irqrestore(&op_table->lock, flags);

	return 0;
}

unsigned long remap_to_uncacheable(unsigned long add, int size)
{
	 unsigned long temp;
	temp = (unsigned long)__pa(add);
	temp = (unsigned long)ioremap(temp, size);
	return temp;
}

#ifdef CONFIG_PM
static int pxa168_freq_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int pxa168_freq_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define pxa168_freq_suspend    NULL
#define pxa168_freq_resume     NULL
#endif

static int pxa168_freq_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct pxa168_dvfm_info *info;
	uint32_t apmu_temp;

	if (!(info = kzalloc(sizeof(struct pxa168_dvfm_info), GFP_KERNEL)))
		goto err;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmum_regs");
	if (!res) goto err;
	info->pmum_base = ioremap(res->start, res->end - res->start + 1);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmua_regs");
	if (!res) goto err;
	info->pmua_base = ioremap(res->start, res->end - res->start + 1);

	pxa168_driver.priv = info;

	if(cpu_is_pxa168_A0() == 0) {
	/* these two bits (30:29) should be set to 0x10 in reg address 0xd4282800 */
		apmu_temp = readl(info->pmua_base);
		apmu_temp |= 0x20000000;
		writel(apmu_temp, info->pmua_base);
		pr_debug(">>>>>%s: PHY(%x) = %x\n", __func__,
			0xd4282800, apmu_temp);
	}

	if (op_init(info, &pxa168_dvfm_op_list))
		goto err;

	if (dvfm_register("PXA168-DVFM", &dvfm_dev_id))
		goto err;
	return dvfm_register_driver(&pxa168_driver, &pxa168_dvfm_op_list);
err:
	printk("pxa168_dvfm init failed\n");
	return -EIO;
}

static int pxa168_freq_remove(struct platform_device *pdev)
{
	kfree(pxa168_driver.priv);
	dvfm_unregister("PXA168-DVFM", &dvfm_dev_id);
	return dvfm_unregister_driver(&pxa168_driver);
}

static struct platform_driver pxa168_freq_driver = {
	.driver = {
		.name	= "pxa168-freq",
	},
	.probe		= pxa168_freq_probe,
	.remove		= pxa168_freq_remove,
#ifdef CONFIG_PM
	.suspend	= pxa168_freq_suspend,
	.resume		= pxa168_freq_resume,
#endif
};

#ifdef CONFIG_MSPM_PXA168_STATS
static unsigned int switch_lowpower_before, switch_lowpower_after;
/* It's invoked by PM functions.
 * PM functions can store the accurate time of entering/exiting low power
 * mode.
 */
int calc_switchtime(unsigned int end, unsigned int start)
{
	switch_lowpower_before = end;
	switch_lowpower_after = start;
	return 0;
}

static int pxa168_stats_notifier_freq(struct notifier_block *nb,
				unsigned long val, void *data)
{
	struct dvfm_freqs *freqs = (struct dvfm_freqs *)data;
	struct op_info *info = &(freqs->new_info);
	struct pxa168_md_opt *md = NULL;
	unsigned int ticks;

	ticks = pxa168_read_32k_ticks();
	md = (struct pxa168_md_opt *)(info->op);
	if (md->power_mode == POWER_MODE_ACTIVE) {
		switch (val) {
		case DVFM_FREQ_PRECHANGE:
			calc_switchtime_start(freqs->old, freqs->new, ticks);
			break;
		case DVFM_FREQ_POSTCHANGE:
			/* Calculate the costed time on switching frequency */
			calc_switchtime_end(freqs->old, freqs->new, ticks);
			dvfm_add_event(freqs->old, CPU_STATE_RUN,
					freqs->new, CPU_STATE_RUN);
			dvfm_add_timeslot(freqs->old, CPU_STATE_RUN);
			mspm_add_event(freqs->old, CPU_STATE_RUN);
			break;
		}
	} else if (md->power_mode == POWER_MODE_APPS_IDLE ||
		md->power_mode == POWER_MODE_APPS_SLEEP ||
		md->power_mode == POWER_MODE_SYS_SLEEP) {
		switch (val) {
		case DVFM_FREQ_PRECHANGE:
			calc_switchtime_start(freqs->old, freqs->new, ticks);
			/* Consider lowpower mode as idle mode */
			dvfm_add_event(freqs->old, CPU_STATE_RUN,
					freqs->new, CPU_STATE_IDLE);
			dvfm_add_timeslot(freqs->old, CPU_STATE_RUN);
			mspm_add_event(freqs->old, CPU_STATE_RUN);
			break;
		case DVFM_FREQ_POSTCHANGE:
			/* switch_lowpower_start before switch_lowpower_after
			 * is updated in calc_switchtime().
			 * It's invoked in pm function.
			 */
			calc_switchtime_end(freqs->old, freqs->new,
					switch_lowpower_before);
			calc_switchtime_start(freqs->new, freqs->old,
					switch_lowpower_after);
			calc_switchtime_end(freqs->new, freqs->old,
					ticks);
			dvfm_add_event(freqs->new, CPU_STATE_IDLE,
					freqs->old, CPU_STATE_RUN);
			dvfm_add_timeslot(freqs->new, CPU_STATE_IDLE);
			mspm_add_event(freqs->new, CPU_STATE_IDLE);
			break;
		}
	}
	return 0;
}
static struct notifier_block notifier_freq_block = {
	.notifier_call = pxa168_stats_notifier_freq,
};
#else
#endif


static int __init pxa168_freq_init(void)
{
	unsigned long base;
	unsigned int size;
	if (!cpu_is_pxa168())
		return -EIO;
#ifdef CONFIG_MSPM_PXA168_STATS
	dvfm_register_notifier(&notifier_freq_block,
				DVFM_FREQUENCY_NOTIFIER);
#endif
	pxa168_dfc_get_lockcache_location(&base, &size);
	dfc_lock_cache_code_base = remap_to_uncacheable(base, size);

	return platform_driver_register(&pxa168_freq_driver);
}

static void __exit pxa168_freq_exit(void)
{
#ifdef CONFIG_MSPM_PXA168_STATS
	dvfm_unregister_notifier(&notifier_freq_block,
				DVFM_FREQUENCY_NOTIFIER);
#endif

	platform_driver_unregister(&pxa168_freq_driver);
}

module_init(pxa168_freq_init);
module_exit(pxa168_freq_exit);

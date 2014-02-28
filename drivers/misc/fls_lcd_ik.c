/*  
 * FLS front panel lcd driver
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define MODULE_NAME "FLS front panel LCD"

#ifdef MODULE
#define DEVNODE
#define LCD_SPLASH_MSG ""
#else
#define LCD_SPLASH_MSG "\eJ\n\r    BOOTING    \n\r (PLEASE  WAIT)\n\r"
#endif

#define LCD_HW_RESET 1

static int major = 0;		/* default to dynamic major */
module_param(major, int, S_IRUGO);
MODULE_PARM_DESC(major, "Major device number");

static int hw_reset = LCD_HW_RESET;
module_param(hw_reset, int, S_IRUGO);
MODULE_PARM_DESC(hw_reset, "reset the lcd on init, or assume it is already configured");

static char *splash_msg = LCD_SPLASH_MSG;
module_param(splash_msg, charp, S_IRUGO);
MODULE_PARM_DESC(splash_msg, "The message to display on the LCD when the module loads");

// hw layout
#define SYSCON_BASE (0x80004000)
#define RS	(1 << 6)
#define RW	(1 << 7)
#define E	(1 << 8)
#define D4	(1 << 0)
#define D5	(1 << 1)
#define D6	(1 << 4)
#define D7	(1 << 5)
#define PWR	(1 << 9)

// mapping for dram address to position on screen
#define LINE_LENGTH (0x10)
#define LINE1_START (0x00)
#define LINE2_START (0x40)
#define LINE3_START (0x10)
#define LINE4_START (0x50)

// timings from datasheet (plus tm to add a little margin so we are safe)
#define Tpor0     (50) // ms delay
#define Tpor1     (5)  // ms delay
#define Tpor2     (200)
#define Tpor3     (200)
#define Tpor4     (200)
#define Tc        (500)
#define Tpw       (230)
#define Tr        (20)
#define Tf        (20)
#define Tsp1      (40)
#define Tsp2      (80)
#define Td        (120)
#define Tm        (50)

struct dio_reg_t {
	unsigned long paddr;
	size_t size;
	struct resource *res;
	void __iomem *vaddr;
};

static struct dio_t {
	struct dio_reg_t dir;	
	struct dio_reg_t in;	
	struct dio_reg_t out;	
} lcd_dio = {
	.dir = {.paddr = SYSCON_BASE + 0x1e, .size = 2},
	.in  = {.paddr = SYSCON_BASE + 0x26, .size = 2},
	.out = {.paddr = SYSCON_BASE + 0x16, .size = 2},
};

enum lcd_busy_state {
	lcd_idle = 0x00,
	lcd_busy = 0x80,
};

enum lcd_display {
	lcd_display_off = 0x00,
	lcd_display_on = 0x04,
};

enum lcd_cursor {
	lcd_cursor_off = 0x00,
	lcd_cursor_on = 0x02,
};

enum lcd_blink {
	lcd_blink_off = 0x00,
	lcd_blink_on = 0x01,
};

enum lcd_lines {
	lcd_lines_1 = 0x00,
	lcd_lines_2 = 0x08,
};

enum lcd_font {
	lcd_font_5by8 = 0x00,
	lcd_font_5by11 = 0x04,
};

enum lcd_data_len {
	lcd_4bit = 0x00,
	lcd_8bit = 0x10,
};

enum lcd_id {
	lcd_id_left = 0x00,
	lcd_id_right = 0x02,
};

enum lcd_sh {
	lcd_sh_off = 0x00,
	lcd_sh_on = 0x01,
};

enum write_state {
	WRITE_STATE_NORMAL,
	WRITE_STATE_ESCAPE1
};

static struct lcd_t {
	struct dio_t *dio;
	int pos;
	enum write_state wstate;
	enum lcd_display display_state;
	enum lcd_cursor cursor_state;
	enum lcd_blink blink_state;
	bool am;
} lcd = {
	.dio = &lcd_dio,
	.pos = 0,
	.wstate = WRITE_STATE_NORMAL,
	.am = true,
};

static void dio_set(struct dio_t *dio, unsigned int set_mask, unsigned int clear_mask)
{
	unsigned int dir, out;
	unsigned long flags;
	unsigned int output_mask = set_mask | clear_mask;

	// make these operations seemly atomic (at least
	// on our single core system)
	local_irq_save(flags);

	// set and clear output state
	out = ioread16(dio->out.vaddr);
	mb();
	out |= set_mask;
	out &= ~clear_mask;
	iowrite16(out, dio->out.vaddr);
	mb();

	// ensure these pins are outputs (if already inputs they will
	// all switch together, if some were inputs and some where
	// outputs there might be a slight glitch between some pins
	// and if all were outputs this step does nothing effectively
	dir = ioread16(dio->dir.vaddr);
	mb();
	dir |= output_mask; // 1 = output, 0 = input
	iowrite16(dir, dio->dir.vaddr);
	mb();

	local_irq_restore(flags);
}

static unsigned int dio_get(struct dio_t *dio, unsigned int get_mask)
{
	unsigned int dir, in;
	unsigned long flags;

	// make these operations seemly atomic (at least
	// on our single core system)
	local_irq_save(flags);

	// ensure these pins are inputs
	dir = ioread16(dio->dir.vaddr);
	mb();
	dir &= ~get_mask; // 1 = output, 0 = input
	iowrite16(dir, dio->dir.vaddr);
	mb();

	// set and clear output state
	in = ioread16(dio->in.vaddr);
	mb();
	in &= get_mask;
	
	local_irq_restore(flags);
	return in;
}

static int dio_init(struct dio_t *dio)
{
	// sanity check (can remove after devel phase)
	if (!dio) {
		printk(KERN_ERR "invalid parameter\n");
		return -EFAULT;
	}

	// request dir, in, out regions
	dio->dir.res = request_region(dio->dir.paddr, dio->dir.size, MODULE_NAME);
	if (!dio->dir.res) {
		printk(KERN_ERR "requested io region (%.8lx) is in use\n", dio->dir.paddr);
		return -EBUSY;
	}
	dio->in.res = request_region(dio->in.paddr, dio->in.size, MODULE_NAME);
	if (!dio->in.res) {
		printk(KERN_ERR "requested io region (%.8lx) is in use\n", dio->in.paddr);
		return -EBUSY;
	}
	dio->out.res = request_region(dio->out.paddr, dio->out.size, MODULE_NAME);
	if (!dio->out.res) {
		printk(KERN_ERR "requested io region (%.8lx) is in use\n", dio->out.paddr);
		return -EBUSY;
	}
	
	// get virtual address of dir, in, out from phys address so we may use them
	dio->dir.vaddr = ioremap_nocache(dio->dir.paddr, dio->dir.size);
	if (!dio->dir.vaddr) {
		printk(KERN_ERR "unable to remap io region (%.8lx)\n", dio->dir.paddr);
		return -EFAULT;
	}
	dio->in.vaddr = ioremap_nocache(dio->in.paddr, dio->in.size);
	if (!dio->in.vaddr) {
		printk(KERN_ERR "unable to remap io region (%.8lx)\n", dio->in.paddr);
		return -EFAULT;
	}
	dio->out.vaddr = ioremap_nocache(dio->out.paddr, dio->out.size);
	if (!dio->out.vaddr) {
		printk(KERN_ERR "unable to remap io region (%.8lx)\n", dio->out.paddr);
		return -EFAULT;
	}
	
	return 0;
}

static void dio_deinit(struct dio_t *dio)
{
	// unmap virtual addresses of dir, in, out
	if (dio->dir.vaddr)
		iounmap(dio->dir.vaddr);
	dio->dir.vaddr = NULL;
	if (dio->in.vaddr)
		iounmap(dio->in.vaddr);
	dio->in.vaddr = NULL;
	if (dio->out.vaddr)
		iounmap(dio->out.vaddr);
	dio->out.vaddr = NULL;

	// release memory regions
	if (dio->dir.res)
		release_region(dio->dir.paddr, dio->dir.size);
	dio->dir.res = NULL;
	if (dio->in.res)
		release_region(dio->in.paddr, dio->in.size);
	dio->in.res = NULL;
	if (dio->out.res)
		release_region(dio->out.paddr, dio->out.size);
	dio->out.res = NULL;
}

#define cond_to_dio_masks(cond, set, clear, bit) {if (cond) set |= bit; else clear |= bit;}
static void lcd_write4(struct lcd_t *lcd, uint8_t rs, uint8_t db)
{
	unsigned int set = 0;
	unsigned int clear = 0;

	// set rw = 0 (write), and rs
	cond_to_dio_masks(rs, set, clear, RS);
	dio_set(lcd->dio, set, clear | RW);

	// wait for >= tsp1
	ndelay(Tsp1 - Tr + Tm);

	// set e hi
	dio_set(lcd->dio, E, 0);
	ndelay(Tr + Tm);

	// hold e hi for >= tpw - tsp2
	ndelay(Tpw - Tsp2 + Tm);

	// set/clear db
	set = 0;
	clear = 0;
	cond_to_dio_masks((db & (1 << 4)), set, clear, D4);
	cond_to_dio_masks((db & (1 << 5)), set, clear, D5);
	cond_to_dio_masks((db & (1 << 6)), set, clear, D6);
	cond_to_dio_masks((db & (1 << 7)), set, clear, D7);
	dio_set(lcd->dio, set, clear);
	
	// hack for TS8500: even though u10 is powered off it still adds a lot of capacitance
	// the d5 line, this takes 5us to die away so we add a 10us delay here to handle that
	// on the real fls this should not be needed as there is no u10
	udelay(50);
	
	// hold db and enable for >= tps2
	ndelay(Tsp2 + Tm);

	// set e lo
	dio_set(lcd->dio, 0, E);
	ndelay(Tf + Tm);

	// wait for >= thd1 + tf
	ndelay(Tc - Tr - Tpw - Tf + Tm);
}

static void lcd_write8(struct lcd_t *lcd, uint8_t rs, uint8_t db)
{
	lcd_write4(lcd, rs, db);        // upper nibble first
	lcd_write4(lcd, rs, db << 4);   // then lower nibble
}

static uint8_t lcd_read4(struct lcd_t *lcd, uint8_t rs)
{
	uint8_t db = 0, tmp;
	unsigned int set = 0;
	unsigned int clear = 0;

	// set rw = 1 (read), and rs
	cond_to_dio_masks(rs, set, clear, RS);
	dio_set(lcd->dio, set | RW, clear);

	// wait for >= tsp1
	ndelay(Tsp1 - Tr + Tm);

	// set e hi
	dio_set(lcd->dio, E, 0);
	ndelay(Tr + Tm);

	// hold e hi for >= tpw - tsp2
	ndelay(Td - Tr + Tm);

	// hack for TS8500: even though u10 is powered off it still adds a lot of capacitance
	// the d5 line, this takes 5us to die away so we add a 10us delay here to handle that
	// on the real fls this should not be needed as there is no u10
	udelay(50);

	// set/clear db
	tmp = dio_get(lcd->dio, D4 | D5 | D6 | D7);
	db |= tmp & D4 ? (1 << 4): 0;
	db |= tmp & D5 ? (1 << 5): 0;
	db |= tmp & D6 ? (1 << 6): 0;
	db |= tmp & D7 ? (1 << 7): 0;
	
	// hold db and enable for >= tps2
	ndelay(Tpw + Tr - Td + Tm);
	
	// set e lo
	dio_set(lcd->dio, 0, E);
	ndelay(Tf + Tm);

	// wait for >= thd1 + tf
	ndelay(Tc - Tr - Tpw - Tf + Tm);

	return db;
}

static uint8_t lcd_read8(struct lcd_t *lcd, uint8_t rs)
{
	uint8_t db = 0;
	db |= (lcd_read4(lcd, rs) >> 0) & 0xf0;
	db |= (lcd_read4(lcd, rs) >> 4) & 0x0f;
	return db;
}

static void lcd_power_cycle(struct lcd_t *lcd)
{
	// ensure power is off for enough time for the lcd to power down
	dio_set(lcd->dio, 0, PWR);
	mdelay(Tpor0);

	// power on 
	dio_set(lcd->dio, PWR, 0);
	mdelay(Tpor0);
}

static enum lcd_busy_state lcd_is_busy(struct lcd_t *lcd, uint8_t *addr)
{
	uint8_t db;
	db = lcd_read8(lcd, 0);
	if (addr)
		*addr = db & 0x7f;
	return db & 0x80;
}

static int lcd_busy_wait(struct lcd_t *lcd)
{
	int t = 0;
	while (t < 10000) { // wait up to 10ms max for lcd to be ready
		if (lcd_is_busy(lcd, NULL) == lcd_idle)
			return 0;
		udelay(500);
		t += 500;
	}
	if (lcd_is_busy(lcd, NULL) == lcd_idle)
		return 0;
	printk(KERN_ERR "timed-out waiting for lcd to return from busy state\n");
	return -1;
}

void lcd_display_control(struct lcd_t *lcd, enum lcd_display d, enum lcd_cursor c, enum lcd_blink b)
{
	uint8_t db = 0x08;	// display control 

	// build command
	db |= d | c | b;

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);

	// update states
	lcd->display_state = d;
	lcd->cursor_state = c;
	lcd->blink_state = b;
}

static void lcd_function_set(struct lcd_t *lcd, enum lcd_lines n, enum lcd_font f)
{
	// warning I think this function (or parts of it) may only work at power on
	// see the datasheet (section 4-bit interface mode, p16)
	uint8_t db = 0x20;	// function set

	// build command
	db |= lcd_4bit | n | f;

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);
}

void lcd_cursor(struct lcd_t *lcd, bool enable)
{
	lcd->cursor_state = enable ? lcd_cursor_on: lcd_cursor_off;
	lcd_display_control(lcd, lcd->display_state, lcd->cursor_state, lcd->blink_state);
}

void lcd_blink(struct lcd_t *lcd, bool enable)
{
	lcd->blink_state = enable ? lcd_blink_on: lcd_blink_off;
	lcd_display_control(lcd, lcd->display_state, lcd->cursor_state, lcd->blink_state);
}

static void lcd_clear(struct lcd_t *lcd)
{
	uint8_t db = 0x01;	// display clear

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);
}

static void lcd_home(struct lcd_t *lcd)
{
	uint8_t db = 0x02;	// home 

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);
	lcd->pos = 0;
}

static void lcd_entry_mode(struct lcd_t *lcd, enum lcd_id id, enum lcd_sh sh)
{
	uint8_t db = 0x04;	// entry mode

	// build command
	db |= id | sh;

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);
}

static void lcd_set_dram_addr(struct lcd_t *lcd, uint8_t addr)
{
	uint8_t db = 0x80;	// set dram address 

	// build command
	addr &= 0x7f;
	db |= addr;

	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 0, db);
	lcd->pos = addr;
}

#define LINE1_EOLPP (0x20)
#define LINE2_EOLPP (0x21)
#define LINE3_EOLPP (0x60)
#define LINE4_EOLPP (0x61)
#define LINE1_SOLMM (0x67)
#define LINE2_SOLMM (0x66)
#define LINE3_SOLMM (0x27)
#define LINE4_SOLMM (0x26)
static void lcd_set_am(struct lcd_t *lcd, bool am)
{
	if (am && !lcd->am) {
		// enable am when currently disabled
		switch (lcd->pos) {
			case LINE1_EOLPP:
				lcd_set_dram_addr(lcd, LINE1_START + LINE_LENGTH - 1);
				break;
			case LINE2_EOLPP:
				lcd_set_dram_addr(lcd, LINE2_START + LINE_LENGTH - 1);
				break;
			case LINE3_EOLPP:
				lcd_set_dram_addr(lcd, LINE3_START + LINE_LENGTH - 1);
				break;
			case LINE4_EOLPP:
				lcd_set_dram_addr(lcd, LINE4_START + LINE_LENGTH - 1);
				break;
			case LINE1_SOLMM:
				lcd_set_dram_addr(lcd, LINE1_START);
				break;
			case LINE2_SOLMM:
				lcd_set_dram_addr(lcd, LINE2_START);
				break;
			case LINE3_SOLMM:
				lcd_set_dram_addr(lcd, LINE3_START);
				break;
			case LINE4_SOLMM:
				lcd_set_dram_addr(lcd, LINE4_START);
				break;
			default:
				break;
		}
	} else if (!am && lcd->am) {
		// disable am (do nothing, we are already at a valid location
		// and will just roll off the eol or sol naturally
	}
	
	lcd->am = am;
}

static void lcd_inc_pos(struct lcd_t *lcd)
{
	if (lcd->am) {
		// automatic margins (wrap to next line)
		switch (++lcd->pos) {
			case LINE1_START + LINE_LENGTH:
				// end of line 1, goto line 2
				lcd_set_dram_addr(lcd, LINE2_START);
				break;
			case LINE2_START + LINE_LENGTH:
				// end of line 2, goto line 3
				lcd_set_dram_addr(lcd, LINE3_START);
				break;
			case LINE3_START + LINE_LENGTH:
				// end of line 3, goto line 4
				lcd_set_dram_addr(lcd, LINE4_START);
				break;
			case LINE4_START + LINE_LENGTH:
				// end of line 4, goto line 1
				lcd_set_dram_addr(lcd, LINE1_START);
				break;
			default:
				break;
		}
	} else {
		// no automatic margins.
		switch (lcd->pos) {
			case LINE1_EOLPP:
			case LINE2_EOLPP:
			case LINE3_EOLPP:
			case LINE4_EOLPP:
			case LINE1_SOLMM:
			case LINE2_SOLMM:
			case LINE3_SOLMM:
			case LINE4_SOLMM:
				// stay put when am = false and we ran off a line
				lcd_set_dram_addr(lcd, lcd->pos);
				return;
			default:
				break;
		}
		switch (++lcd->pos) {
			case LINE1_START + LINE_LENGTH:
				// end of line 1, pin to eol
				lcd_set_dram_addr(lcd, LINE1_EOLPP);
				break;
			case LINE2_START + LINE_LENGTH:
				// end of line 2, pin to eol
				lcd_set_dram_addr(lcd, LINE2_EOLPP);
				break;
			case LINE3_START + LINE_LENGTH:
				// end of line 3, pin to eol
				lcd_set_dram_addr(lcd, LINE3_EOLPP);
				break;
			case LINE4_START + LINE_LENGTH:
				// end of line 4, pin to eol
				lcd_set_dram_addr(lcd, LINE4_EOLPP);
				break;
			default:
				break;
		}
	}
}

static void lcd_dec_pos(struct lcd_t *lcd)
{
	if (lcd->am) {
		switch (--lcd->pos) {
			case LINE1_START - 1:
				// start of line 1, goto line 4
				lcd_set_dram_addr(lcd, LINE4_START + LINE_LENGTH - 1);
				break;
			case LINE2_START - 1:
				// start of line 2, goto line 1
				lcd_set_dram_addr(lcd, LINE1_START + LINE_LENGTH - 1);
				break;
			case LINE3_START - 1:
				// start of line 3, goto line 2
				lcd_set_dram_addr(lcd, LINE2_START + LINE_LENGTH - 1);
				break;
			case LINE4_START - 1:
				// start of line 4, goto line 3
				lcd_set_dram_addr(lcd, LINE3_START + LINE_LENGTH - 1);
				break;
			default:
				break;
		}
	} else {
		// no automatic margins.
		switch (lcd->pos) {
			case LINE1_EOLPP:
			case LINE2_EOLPP:
			case LINE3_EOLPP:
			case LINE4_EOLPP:
			case LINE1_SOLMM:
			case LINE2_SOLMM:
			case LINE3_SOLMM:
			case LINE4_SOLMM:
				// stay put when am = false and we ran off a line
				lcd_set_dram_addr(lcd, lcd->pos);
				return;
			default:
				break;
		}
		switch (--lcd->pos) {
			case LINE1_START - 1:
				// start of line 1, pin to sol
				lcd_set_dram_addr(lcd, LINE1_SOLMM);
				break;
			case LINE2_START - 1:
				// start of line 2, pin to sol
				lcd_set_dram_addr(lcd, LINE2_SOLMM);
				break;
			case LINE3_START - 1:
				// start of line 3, pin to sol
				lcd_set_dram_addr(lcd, LINE3_SOLMM);
				break;
			case LINE4_START - 1:
				// start of line 4, pin to sol
				lcd_set_dram_addr(lcd, LINE4_SOLMM);
				break;
			default:
				break;
		}
	}
}

#define LINE_MASK (LINE1_START | LINE2_START | LINE3_START | LINE4_START)
void lcd_getxy(struct lcd_t *lcd, int *x, int *y)
{
	bool am = lcd->am;

	lcd_set_am(lcd, true);
	*x = lcd->pos & 0x0f;
	switch (lcd->pos & LINE_MASK) {
		case LINE1_START:
			*y = 0;
			break;
		case LINE2_START:
			*y = 1;
			break;
		case LINE3_START:
			*y = 2;
			break;
		case LINE4_START:
			*y = 3;
			break;
		default:
			// how did we get here !!
			*y = 0;
			break;
	}
	lcd_set_am(lcd, am);
}

enum whence_t {WHENCE_ABS, WHENCE_REL};
int lcd_gotoxy(struct lcd_t *lcd, int x, int y, enum whence_t whence)
{
	int r = 0;
	int dp;
	bool am = lcd->am;

	switch (whence) {
		case WHENCE_ABS:
			// bounds check x,y
			if (y * LINE_LENGTH + x > 4 * LINE_LENGTH)
				return -1;
			lcd_home(lcd);
			// now we are at 0 fall through to relative moves

		case WHENCE_REL:
			// x,y are not bounds check on relative moves (they just wrap)
			dp = y * LINE_LENGTH + x;
			if (dp < 0) {
				lcd_set_am(lcd, true);
				while (dp++ != 0)
					lcd_dec_pos(lcd);
				lcd_set_am(lcd, am);
			}
			else if (dp > 0) {
				lcd_set_am(lcd, true);
				while (dp-- != 0)
					lcd_inc_pos(lcd);
				lcd_set_am(lcd, am);
			}
			break;
	}

	lcd_set_dram_addr(lcd, lcd->pos);
	return r;
}

static void lcd_putchar(struct lcd_t *lcd, char c)
{
	// wait for the lcd to be ready before sending the command
	lcd_busy_wait(lcd);
	lcd_write8(lcd, 1, c);
	lcd_inc_pos(lcd);
}

static void lcd_4bit_init(struct lcd_t *lcd, enum lcd_lines lines, enum lcd_font font)
{
	// force us into 8 bit mode (just to get to a known sync point)
	lcd_write4(lcd, 0, 0x30);
	mdelay(Tpor1);
	lcd_write4(lcd, 0, 0x30);
	udelay(Tpor2);
	lcd_write4(lcd, 0, 0x30);
	udelay(Tpor3);

	// goto 4 bit mode (setting the number of lines and font)
	// note we are only able to run this function set at this stage and never again (see datasheet p16, and
	// http://www.piclist.com/techref/postbot.asp?by=thread&id=HD44780+LCD+and+4-bit+mode+using+16F84&w=body&tgt=post)
	lcd_write4(lcd, 0, 0x20);
	udelay(Tpor4);
	
	// set initial startup settings recommended in the datasheet
	lcd_function_set(lcd, lcd_lines_2, lcd_font_5by8);
	lcd_display_control(lcd, lcd_display_off, lcd_cursor_off, lcd_blink_off); // turn everything off
	lcd_entry_mode(lcd, lcd_id_right, lcd_sh_off);
}

loff_t lcd_llseek(struct file *filp, loff_t off, int whence)
{
	switch (whence) {
		case 0: // SEEK_SET
			if (off > 4*LINE_LENGTH || off < 0) {
				printk(KERN_ERR "unsupported SEEK_SET offset %llx\n", off);
				return -EINVAL;
			}
			lcd_gotoxy(&lcd, off, 0, WHENCE_ABS);
			break;
		case 1: // SEEK_CUR
			if (off > 4*LINE_LENGTH || off < -4*LINE_LENGTH) {
				printk(KERN_ERR "unsupported SEEK_CUR offset %llx\n", off);
				return -EINVAL;
			}
			lcd_gotoxy(&lcd, off, 0, WHENCE_REL);
			break;
		case 2: // SEEK_END (not supported, hence fall though)
		default:
			// how did we get here !
			printk(KERN_ERR "unsupported seek operation\n");
			return -EINVAL;
	}
	filp->f_pos = lcd.pos;
	return lcd.pos;
}

ssize_t lcd_print(const char *buf, size_t count)
{
	size_t l;
	int x, y;

	for (l = 0; l < count && buf[l] != 0; l++)
	{
		switch (lcd.wstate)
		{
			case WRITE_STATE_NORMAL:
				switch(buf[l])
				{
					case 0x1b:
						// escape char mode !
						lcd.wstate = WRITE_STATE_ESCAPE1;
						break;
					case '\n':
						// new line
						lcd_gotoxy(&lcd, 0, 1, WHENCE_REL);
						break;
					case '\r':
						// cr (goto x = 0)
						lcd_getxy(&lcd, &x, &y);
						lcd_gotoxy(&lcd, -x, 0, WHENCE_REL);
						break;
					case '\t':
						// tab (align to 4 bytes)
						do
						{
							lcd_putchar(&lcd, ' ');
							lcd_getxy(&lcd, &x, &y);
						}
						while (x % 4 != 0);
						break;
					case '\b':
						// backspace
						lcd_gotoxy(&lcd, -1, 0, WHENCE_REL);
						break;
					default:
						// normal characters
						lcd_putchar(&lcd, buf[l]);
						break;
				}
				break;

			case WRITE_STATE_ESCAPE1:
				switch(buf[l])
				{
					case 'a':
						// all attributes off (blink = 0)
						lcd_cursor(&lcd, 0);
						lcd_blink(&lcd, 0);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'b':
						// blink on
						lcd_blink(&lcd, 1);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'v':
						// cursor visible
						lcd_cursor(&lcd, 1);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'V':
						// cursor invisible
						lcd_cursor(&lcd, 0);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'H':
						// home cursor wtf does this mean (0,0 or sol)?
						lcd_gotoxy(&lcd, 0, 0, WHENCE_ABS);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'J':
						// clear screen and home the cursor
						lcd_clear(&lcd);
						lcd_gotoxy(&lcd, 0, 0, WHENCE_ABS);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'B':
						// move down 1
						lcd_gotoxy(&lcd, 0, 1, WHENCE_REL);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'A':
						// move up 1
						lcd_gotoxy(&lcd, 0, -1, WHENCE_REL);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'D':
						// move left 1
						lcd_gotoxy(&lcd, -1, 0, WHENCE_REL);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'C':
						// move right 1
						lcd_gotoxy(&lcd, 1, 0, WHENCE_REL);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'm':
						lcd_set_am(&lcd, true);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					case 'M':
						lcd_set_am(&lcd, false);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
					default:
						// unknown escape code (just dump the output)
						printk(KERN_WARNING "unknown escape code %.2x\n", buf[l]);
						lcd.wstate = WRITE_STATE_NORMAL;
						break;
				}
				break;
		}
	}

	return l;
}

ssize_t lcd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int ret = 0;
	char *_buf = NULL;

	// buffer the user data local so we loop through
	// without needing to be able to sleep
	_buf = kmalloc(count, GFP_KERNEL);
	if (!_buf) {
		printk(KERN_ERR "unable to alloc write buffer\n");
		ret = -ENOMEM;
		goto exit;
	}
	memset(_buf, 0, count);
	if (copy_from_user(_buf, buf, count)) {
		// do not support partial writes (this might cause the lcd to flicker for one)
		printk(KERN_ERR "bad write buffer, write rejected\n");
		ret = -EFAULT;
		goto exit;
	}

	// process buffer
	lcd_print(_buf, count);
	*f_pos = lcd.pos;

	// success
	ret = count;

exit:
	if (_buf)
		kfree(_buf);
	return ret;
}

static atomic_t lcd_available = ATOMIC_INIT(1);

int lcd_open(struct inode *inode, struct file *filp)
{
	if (!atomic_dec_and_test(&lcd_available)) {
		atomic_inc(&lcd_available);
		return -EBUSY; // already open
	}

	return 0;
}

int lcd_release(struct inode *inode, struct file *filp)
{
	atomic_inc(&lcd_available);
	return 0;
}

#ifdef DEVNODE
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.write = lcd_write,
	.llseek = lcd_llseek,
	.open = lcd_open,
	.release = lcd_release,
};

static struct class *cl;

static struct cdev cdev;
#endif

int lcd_init(void)
{
	int ret = 0;
#ifdef DEVNODE
	dev_t devno;
	struct device *device;
#endif

	// start up msg
	printk(KERN_INFO "FLS LCD driver started\n");

	// init the registers etc
	ret = dio_init(lcd.dio);
	if (ret < 0) {
		printk(KERN_ERR "lcd module unable to init dio, bailing out\n");
		goto fail;
	}

	// if hw_reset then we need to power cycle if we can and then resync via 
	// a 4 bit init, then reset-up the screen settings the way we want them
	if (hw_reset) {
		// set power switch lo so the lcd looses power, then hi again until it turns on
		lcd_power_cycle(&lcd);

		// do 4 bit init sequence (see datasheet, p16)
		// this ensures we get in sync with the lcd
		lcd_4bit_init(&lcd, lcd_lines_2, lcd_font_5by8);

		// now do our init
		lcd_clear(&lcd);
		lcd_home(&lcd);
		lcd_display_control(&lcd, lcd_display_on, lcd_cursor_off, lcd_blink_off);
	} else {
		// just home the cursor if we are not doing a full reset, this way at least we know where we are
		lcd_home(&lcd);
	}

	// show initial splash screen
	if (strlen(splash_msg) > 0)
		lcd_print(splash_msg, strlen(splash_msg));

#ifdef DEVNODE
	// allocate a new dev number (this can be dynamic or
	// static if passed in as a module param)
	if (major) {
		devno = MKDEV(major, 0);
		ret = register_chrdev_region(devno, 1, MODULE_NAME);
	} else {
		ret = alloc_chrdev_region(&devno, 0, 1, MODULE_NAME);
		major = MAJOR(devno);
	}
	if (ret < 0) {
		printk(KERN_ERR "alloc_chrdev_region failed\n");
		goto fail;
	}

	// create a dummy class for the lcd
	cl = class_create(THIS_MODULE, "lcd");
	if (IS_ERR(cl)) {
		printk(KERN_ERR "class_simple_create for class lcd failed\n");
		goto fail1;
	}

	// create cdev interface
	cdev_init(&cdev, &fops);
	cdev.owner = THIS_MODULE;
	ret = cdev_add(&cdev, devno, 1);
	if (ret) {
		printk(KERN_ERR "cdev_add failed\n");
		goto fail2;
	}

	// create /sys/lcd/fplcd/dev so udev will add our device to /dev/fplcd
	device = device_create(cl, NULL, devno, NULL, "lcd");
	if (IS_ERR(device)) {
		printk(KERN_ERR "device_create for fplcd failed\n");
		goto fail3;
	}

	return 0;
#endif

#ifdef DEVNODE
fail3:
	cdev_del(&cdev);
fail2:
	class_destroy(cl);
fail1:
	unregister_chrdev_region(devno, 1);
#endif
fail:
	// deinit registers etc
	dio_deinit(lcd.dio);
	return ret;
}

void lcd_cleanup(void)
{
#ifdef DEVNODE
	// clean up device node
	device_destroy(cl, MKDEV(major, 0));
	cdev_del(&cdev);
	class_destroy(cl);
	unregister_chrdev_region(MKDEV(major, 0), 1);
#endif

	// deinit registers etc
	dio_deinit(lcd.dio);

	// shutdown msg
	printk(KERN_INFO "FLS LCD driver done\n");
}

#ifdef MODULE
module_init(lcd_init);
#else 

// if we are not a module then load this driver asap
// so the splash msg is shown quickly
#ifndef DEVNODE
early_initcall(lcd_init);
#else
pure_initcall(lcd_init);
#endif

#endif
module_exit(lcd_cleanup);

MODULE_LICENSE("GPL");


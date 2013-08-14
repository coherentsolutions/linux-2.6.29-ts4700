#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/io.h>


#define FPGA_SYSCON_BASE  0x80004000
#define NR_UARTS  2

static struct uart_port req[NR_UARTS];

  
static char io[16]; 
static char irq[16];

static int __init ts4700_isa16550_init(void)
{
	int line;
	unsigned int uart_irq[NR_UARTS];
	unsigned int uart_adr[NR_UARTS];
	
	uart_adr[0] = 0x2e8;
	uart_irq[0] = 5;
	
	uart_adr[1] = 0x3e8;   	
	uart_irq[1] = 6;
	
	if (strlen(io)) {
	   char *cp;
	   uart_adr[0] = simple_strtoul(io, &cp, 0);
	   if (cp && *cp)
	      uart_adr[1] = simple_strtoul(&cp[1], &cp, 0);	   
	}
	 		
	//printk("Using 0x%08lX and 0x%08lX\n", uart_adr[0], uart_adr[1]);
	
	if (strlen(irq)) {
	   char *cp;
	   uart_irq[0] = simple_strtoul(irq, &cp, 0);
	   if (cp && *cp)
	      uart_irq[1] = simple_strtoul(&cp[1], &cp, 0);	   
	}
	
	//printk("Using irq %d and %d\n", uart_irq[0], uart_irq[1]);
		
	req[0].type = PORT_16550A;
	req[0].iotype = UPIO_MEM;
	req[0].iobase = 0;
	req[0].fifosize = 16;
	req[0].flags = UPF_IOREMAP | UPF_SHARE_IRQ;
	req[0].regshift = 0;
	req[0].irq = 62 + uart_irq[0];
	req[0].mapbase = 0x81008000UL + uart_adr[0]; 
	req[0].membase = (char *)req[0].mapbase;
	req[0].uartclk = 1843200;
	line = serial8250_register_port(&req[0]);
				
	req[1].type = PORT_16550A;
	req[1].iotype = UPIO_MEM;
	req[1].iobase = 0;
	req[1].fifosize = 16;
	req[1].flags = UPF_IOREMAP | UPF_SHARE_IRQ;
	req[1].regshift = 0;
	req[1].irq = 62 + uart_irq[1];
	req[1].mapbase = 0x81008000UL + uart_adr[1]; 
	req[1].membase = (char *)req[1].mapbase;
	req[1].uartclk = 1843200;
	line = serial8250_register_port(&req[1]);
	
	if (line < 0)
      return line;
   else
      return 0;
}

static void __exit ts4700_isa16550_exit(void)
{
  
   
}

module_init(ts4700_isa16550_init);
module_exit(ts4700_isa16550_exit);
module_param_string(io, io, sizeof(io), 0644);
module_param_string(irq, irq, sizeof(irq), 0644);
MODULE_LICENSE("GPL");

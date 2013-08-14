#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <asm/io.h>


extern u8 pxa168_pcie_read8(u32 addr);
extern void pxa168_pcie_write8(u8 val, u32 addr);

void _pxa168_memcpy_toio(volatile void __iomem *to, const void *from,
			 size_t count)
{
	const unsigned char *f = from;
	while (count) {
		count--;
		writeb(*f, to);
		pxa168_pcie_write8(*f, (u32) to);
		f++;
		to++;
	}
}
EXPORT_SYMBOL(_pxa168_memcpy_toio);

void _pxa168_memcpy_fromio(void *to, const volatile void __iomem *from,
			   size_t count)
{
	unsigned char *t = to;
	while (count) {
		count--;
		*t = pxa168_pcie_read8((u32) from) & 0xff;
		t++;
		from++;
	}
}
EXPORT_SYMBOL(_pxa168_memcpy_fromio);

void _pxa168_memset_io(volatile void __iomem *dst, int c, size_t count)
{
	while (count) {
		count--;
		pxa168_pcie_write8(c, (u32) dst);
		dst++;
	}
}
EXPORT_SYMBOL(_pxa168_memset_io);

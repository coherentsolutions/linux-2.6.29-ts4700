#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifdef CONFIG_FUNCTION_TRACER
#ifndef KBUILD_NEW_GNU_MCOUNT
#define MCOUNT_ADDR		((long)(mcount))
#else
#define MCOUNT_ADDR		((long)(__gnu_mcount_nc))
#endif
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
struct dyn_arch_ftrace {};
extern void mcount(void);
extern void __gnu_mcount_nc(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}
#endif

#endif
#endif /* _ASM_ARM_FTRACE */

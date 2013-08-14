# $Id: Makefile,v 1.1 2011/07/05 20:23:16 ian Exp $
#
# Makefile for the linux kernel.
#

obj-y   := dma.o pinmux.o io.o arbiter.o
obj-$(CONFIG_ETRAX_VCS_SIM) += vcs_hook.o
obj-$(CONFIG_CPU_FREQ)   += cpufreq.o

clean:


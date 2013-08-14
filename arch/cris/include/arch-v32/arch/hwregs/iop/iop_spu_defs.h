#ifndef __iop_spu_defs_h
#define __iop_spu_defs_h

/*
 * This file is autogenerated from
 *   file:           ../../inst/io_proc/rtl/iop_spu.r
 *     id:           <not found>
 *     last modfied: Mon Apr 11 16:08:46 2005
 *
 *   by /n/asic/design/tools/rdesc/src/rdes2c --outfile iop_spu_defs.h ../../inst/io_proc/rtl/iop_spu.r
 *      id: $Id: iop_spu_defs.h,v 1.1 2011/07/05 20:23:05 ian Exp $
 * Any changes here will be lost.
 *
 * -*- buffer-read-only: t -*-
 */
/* Main access macros */
#ifndef REG_RD
#define REG_RD( scope, inst, reg ) \
  REG_READ( reg_##scope##_##reg, \
            (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_WR
#define REG_WR( scope, inst, reg, val ) \
  REG_WRITE( reg_##scope##_##reg, \
             (inst) + REG_WR_ADDR_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_VECT
#define REG_RD_VECT( scope, inst, reg, index ) \
  REG_READ( reg_##scope##_##reg, \
            (inst) + REG_RD_ADDR_##scope##_##reg + \
	    (index) * STRIDE_##scope##_##reg )
#endif

#ifndef REG_WR_VECT
#define REG_WR_VECT( scope, inst, reg, index, val ) \
  REG_WRITE( reg_##scope##_##reg, \
             (inst) + REG_WR_ADDR_##scope##_##reg + \
	     (index) * STRIDE_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_INT
#define REG_RD_INT( scope, inst, reg ) \
  REG_READ( int, (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_WR_INT
#define REG_WR_INT( scope, inst, reg, val ) \
  REG_WRITE( int, (inst) + REG_WR_ADDR_##scope##_##reg, (val) )
#endif

#ifndef REG_RD_INT_VECT
#define REG_RD_INT_VECT( scope, inst, reg, index ) \
  REG_READ( int, (inst) + REG_RD_ADDR_##scope##_##reg + \
	    (index) * STRIDE_##scope##_##reg )
#endif

#ifndef REG_WR_INT_VECT
#define REG_WR_INT_VECT( scope, inst, reg, index, val ) \
  REG_WRITE( int, (inst) + REG_WR_ADDR_##scope##_##reg + \
	     (index) * STRIDE_##scope##_##reg, (val) )
#endif

#ifndef REG_TYPE_CONV
#define REG_TYPE_CONV( type, orgtype, val ) \
  ( { union { orgtype o; type n; } r; r.o = val; r.n; } )
#endif

#ifndef reg_page_size
#define reg_page_size 8192
#endif

#ifndef REG_ADDR
#define REG_ADDR( scope, inst, reg ) \
  ( (inst) + REG_RD_ADDR_##scope##_##reg )
#endif

#ifndef REG_ADDR_VECT
#define REG_ADDR_VECT( scope, inst, reg, index ) \
  ( (inst) + REG_RD_ADDR_##scope##_##reg + \
    (index) * STRIDE_##scope##_##reg )
#endif

/* C-code for register scope iop_spu */

#define STRIDE_iop_spu_rw_r 4
/* Register rw_r, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_r;
#define REG_RD_ADDR_iop_spu_rw_r 0
#define REG_WR_ADDR_iop_spu_rw_r 0

/* Register rw_seq_pc, scope iop_spu, type rw */
typedef struct {
  unsigned int addr : 12;
  unsigned int dummy1 : 20;
} reg_iop_spu_rw_seq_pc;
#define REG_RD_ADDR_iop_spu_rw_seq_pc 64
#define REG_WR_ADDR_iop_spu_rw_seq_pc 64

/* Register rw_fsm_pc, scope iop_spu, type rw */
typedef struct {
  unsigned int addr : 12;
  unsigned int dummy1 : 20;
} reg_iop_spu_rw_fsm_pc;
#define REG_RD_ADDR_iop_spu_rw_fsm_pc 68
#define REG_WR_ADDR_iop_spu_rw_fsm_pc 68

/* Register rw_ctrl, scope iop_spu, type rw */
typedef struct {
  unsigned int fsm : 1;
  unsigned int en  : 1;
  unsigned int dummy1 : 30;
} reg_iop_spu_rw_ctrl;
#define REG_RD_ADDR_iop_spu_rw_ctrl 72
#define REG_WR_ADDR_iop_spu_rw_ctrl 72

/* Register rw_fsm_inputs3_0, scope iop_spu, type rw */
typedef struct {
  unsigned int val0 : 5;
  unsigned int src0 : 3;
  unsigned int val1 : 5;
  unsigned int src1 : 3;
  unsigned int val2 : 5;
  unsigned int src2 : 3;
  unsigned int val3 : 5;
  unsigned int src3 : 3;
} reg_iop_spu_rw_fsm_inputs3_0;
#define REG_RD_ADDR_iop_spu_rw_fsm_inputs3_0 76
#define REG_WR_ADDR_iop_spu_rw_fsm_inputs3_0 76

/* Register rw_fsm_inputs7_4, scope iop_spu, type rw */
typedef struct {
  unsigned int val4 : 5;
  unsigned int src4 : 3;
  unsigned int val5 : 5;
  unsigned int src5 : 3;
  unsigned int val6 : 5;
  unsigned int src6 : 3;
  unsigned int val7 : 5;
  unsigned int src7 : 3;
} reg_iop_spu_rw_fsm_inputs7_4;
#define REG_RD_ADDR_iop_spu_rw_fsm_inputs7_4 80
#define REG_WR_ADDR_iop_spu_rw_fsm_inputs7_4 80

/* Register rw_gio_out, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_gio_out;
#define REG_RD_ADDR_iop_spu_rw_gio_out 84
#define REG_WR_ADDR_iop_spu_rw_gio_out 84

/* Register rw_bus0_out, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_bus0_out;
#define REG_RD_ADDR_iop_spu_rw_bus0_out 88
#define REG_WR_ADDR_iop_spu_rw_bus0_out 88

/* Register rw_bus1_out, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_bus1_out;
#define REG_RD_ADDR_iop_spu_rw_bus1_out 92
#define REG_WR_ADDR_iop_spu_rw_bus1_out 92

/* Register r_gio_in, scope iop_spu, type r */
typedef unsigned int reg_iop_spu_r_gio_in;
#define REG_RD_ADDR_iop_spu_r_gio_in 96

/* Register r_bus0_in, scope iop_spu, type r */
typedef unsigned int reg_iop_spu_r_bus0_in;
#define REG_RD_ADDR_iop_spu_r_bus0_in 100

/* Register r_bus1_in, scope iop_spu, type r */
typedef unsigned int reg_iop_spu_r_bus1_in;
#define REG_RD_ADDR_iop_spu_r_bus1_in 104

/* Register rw_gio_out_set, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_gio_out_set;
#define REG_RD_ADDR_iop_spu_rw_gio_out_set 108
#define REG_WR_ADDR_iop_spu_rw_gio_out_set 108

/* Register rw_gio_out_clr, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_gio_out_clr;
#define REG_RD_ADDR_iop_spu_rw_gio_out_clr 112
#define REG_WR_ADDR_iop_spu_rw_gio_out_clr 112

/* Register rs_wr_stat, scope iop_spu, type rs */
typedef struct {
  unsigned int r0  : 1;
  unsigned int r1  : 1;
  unsigned int r2  : 1;
  unsigned int r3  : 1;
  unsigned int r4  : 1;
  unsigned int r5  : 1;
  unsigned int r6  : 1;
  unsigned int r7  : 1;
  unsigned int r8  : 1;
  unsigned int r9  : 1;
  unsigned int r10 : 1;
  unsigned int r11 : 1;
  unsigned int r12 : 1;
  unsigned int r13 : 1;
  unsigned int r14 : 1;
  unsigned int r15 : 1;
  unsigned int dummy1 : 16;
} reg_iop_spu_rs_wr_stat;
#define REG_RD_ADDR_iop_spu_rs_wr_stat 116

/* Register r_wr_stat, scope iop_spu, type r */
typedef struct {
  unsigned int r0  : 1;
  unsigned int r1  : 1;
  unsigned int r2  : 1;
  unsigned int r3  : 1;
  unsigned int r4  : 1;
  unsigned int r5  : 1;
  unsigned int r6  : 1;
  unsigned int r7  : 1;
  unsigned int r8  : 1;
  unsigned int r9  : 1;
  unsigned int r10 : 1;
  unsigned int r11 : 1;
  unsigned int r12 : 1;
  unsigned int r13 : 1;
  unsigned int r14 : 1;
  unsigned int r15 : 1;
  unsigned int dummy1 : 16;
} reg_iop_spu_r_wr_stat;
#define REG_RD_ADDR_iop_spu_r_wr_stat 120

/* Register r_reg_indexed_by_bus0_in, scope iop_spu, type r */
typedef unsigned int reg_iop_spu_r_reg_indexed_by_bus0_in;
#define REG_RD_ADDR_iop_spu_r_reg_indexed_by_bus0_in 124

/* Register r_stat_in, scope iop_spu, type r */
typedef struct {
  unsigned int timer_grp_lo    : 4;
  unsigned int fifo_out_last   : 1;
  unsigned int fifo_out_rdy    : 1;
  unsigned int fifo_out_all    : 1;
  unsigned int fifo_in_rdy     : 1;
  unsigned int dmc_out_all     : 1;
  unsigned int dmc_out_dth     : 1;
  unsigned int dmc_out_eop     : 1;
  unsigned int dmc_out_dv      : 1;
  unsigned int dmc_out_last    : 1;
  unsigned int dmc_out_cmd_rq  : 1;
  unsigned int dmc_out_cmd_rdy : 1;
  unsigned int pcrc_correct    : 1;
  unsigned int timer_grp_hi    : 4;
  unsigned int dmc_in_sth      : 1;
  unsigned int dmc_in_full     : 1;
  unsigned int dmc_in_cmd_rdy  : 1;
  unsigned int spu_gio_out     : 4;
  unsigned int sync_clk12      : 1;
  unsigned int scrc_out_data   : 1;
  unsigned int scrc_in_err     : 1;
  unsigned int mc_busy         : 1;
  unsigned int mc_owned        : 1;
} reg_iop_spu_r_stat_in;
#define REG_RD_ADDR_iop_spu_r_stat_in 128

/* Register r_trigger_in, scope iop_spu, type r */
typedef unsigned int reg_iop_spu_r_trigger_in;
#define REG_RD_ADDR_iop_spu_r_trigger_in 132

/* Register r_special_stat, scope iop_spu, type r */
typedef struct {
  unsigned int c_flag         : 1;
  unsigned int v_flag         : 1;
  unsigned int z_flag         : 1;
  unsigned int n_flag         : 1;
  unsigned int xor_bus0_r2_0  : 1;
  unsigned int xor_bus1_r3_0  : 1;
  unsigned int xor_bus0m_r2_0 : 1;
  unsigned int xor_bus1m_r3_0 : 1;
  unsigned int fsm_in0        : 1;
  unsigned int fsm_in1        : 1;
  unsigned int fsm_in2        : 1;
  unsigned int fsm_in3        : 1;
  unsigned int fsm_in4        : 1;
  unsigned int fsm_in5        : 1;
  unsigned int fsm_in6        : 1;
  unsigned int fsm_in7        : 1;
  unsigned int event0         : 1;
  unsigned int event1         : 1;
  unsigned int event2         : 1;
  unsigned int event3         : 1;
  unsigned int dummy1         : 12;
} reg_iop_spu_r_special_stat;
#define REG_RD_ADDR_iop_spu_r_special_stat 136

/* Register rw_reg_access, scope iop_spu, type rw */
typedef struct {
  unsigned int addr   : 13;
  unsigned int dummy1 : 3;
  unsigned int imm_hi : 16;
} reg_iop_spu_rw_reg_access;
#define REG_RD_ADDR_iop_spu_rw_reg_access 140
#define REG_WR_ADDR_iop_spu_rw_reg_access 140

#define STRIDE_iop_spu_rw_event_cfg 4
/* Register rw_event_cfg, scope iop_spu, type rw */
typedef struct {
  unsigned int addr   : 12;
  unsigned int src    : 2;
  unsigned int eq_en  : 1;
  unsigned int eq_inv : 1;
  unsigned int gt_en  : 1;
  unsigned int gt_inv : 1;
  unsigned int dummy1 : 14;
} reg_iop_spu_rw_event_cfg;
#define REG_RD_ADDR_iop_spu_rw_event_cfg 144
#define REG_WR_ADDR_iop_spu_rw_event_cfg 144

#define STRIDE_iop_spu_rw_event_mask 4
/* Register rw_event_mask, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_event_mask;
#define REG_RD_ADDR_iop_spu_rw_event_mask 160
#define REG_WR_ADDR_iop_spu_rw_event_mask 160

#define STRIDE_iop_spu_rw_event_val 4
/* Register rw_event_val, scope iop_spu, type rw */
typedef unsigned int reg_iop_spu_rw_event_val;
#define REG_RD_ADDR_iop_spu_rw_event_val 176
#define REG_WR_ADDR_iop_spu_rw_event_val 176

/* Register rw_event_ret, scope iop_spu, type rw */
typedef struct {
  unsigned int addr : 12;
  unsigned int dummy1 : 20;
} reg_iop_spu_rw_event_ret;
#define REG_RD_ADDR_iop_spu_rw_event_ret 192
#define REG_WR_ADDR_iop_spu_rw_event_ret 192

/* Register r_trace, scope iop_spu, type r */
typedef struct {
  unsigned int fsm      : 1;
  unsigned int en       : 1;
  unsigned int c_flag   : 1;
  unsigned int v_flag   : 1;
  unsigned int z_flag   : 1;
  unsigned int n_flag   : 1;
  unsigned int seq_addr : 12;
  unsigned int dummy1   : 2;
  unsigned int fsm_addr : 12;
} reg_iop_spu_r_trace;
#define REG_RD_ADDR_iop_spu_r_trace 196

/* Register r_fsm_trace, scope iop_spu, type r */
typedef struct {
  unsigned int fsm      : 1;
  unsigned int en       : 1;
  unsigned int tmr_done : 1;
  unsigned int inp0     : 1;
  unsigned int inp1     : 1;
  unsigned int inp2     : 1;
  unsigned int inp3     : 1;
  unsigned int event0   : 1;
  unsigned int event1   : 1;
  unsigned int event2   : 1;
  unsigned int event3   : 1;
  unsigned int gio_out  : 8;
  unsigned int dummy1   : 1;
  unsigned int fsm_addr : 12;
} reg_iop_spu_r_fsm_trace;
#define REG_RD_ADDR_iop_spu_r_fsm_trace 200

#define STRIDE_iop_spu_rw_brp 4
/* Register rw_brp, scope iop_spu, type rw */
typedef struct {
  unsigned int addr : 12;
  unsigned int fsm  : 1;
  unsigned int en   : 1;
  unsigned int dummy1 : 18;
} reg_iop_spu_rw_brp;
#define REG_RD_ADDR_iop_spu_rw_brp 204
#define REG_WR_ADDR_iop_spu_rw_brp 204


/* Constants */
enum {
  regk_iop_spu_attn_hi                     = 0x00000005,
  regk_iop_spu_attn_lo                     = 0x00000005,
  regk_iop_spu_attn_r0                     = 0x00000000,
  regk_iop_spu_attn_r1                     = 0x00000001,
  regk_iop_spu_attn_r10                    = 0x00000002,
  regk_iop_spu_attn_r11                    = 0x00000003,
  regk_iop_spu_attn_r12                    = 0x00000004,
  regk_iop_spu_attn_r13                    = 0x00000005,
  regk_iop_spu_attn_r14                    = 0x00000006,
  regk_iop_spu_attn_r15                    = 0x00000007,
  regk_iop_spu_attn_r2                     = 0x00000002,
  regk_iop_spu_attn_r3                     = 0x00000003,
  regk_iop_spu_attn_r4                     = 0x00000004,
  regk_iop_spu_attn_r5                     = 0x00000005,
  regk_iop_spu_attn_r6                     = 0x00000006,
  regk_iop_spu_attn_r7                     = 0x00000007,
  regk_iop_spu_attn_r8                     = 0x00000000,
  regk_iop_spu_attn_r9                     = 0x00000001,
  regk_iop_spu_c                           = 0x00000000,
  regk_iop_spu_flag                        = 0x00000002,
  regk_iop_spu_gio_in                      = 0x00000000,
  regk_iop_spu_gio_out                     = 0x00000005,
  regk_iop_spu_gio_out0                    = 0x00000008,
  regk_iop_spu_gio_out1                    = 0x00000009,
  regk_iop_spu_gio_out2                    = 0x0000000a,
  regk_iop_spu_gio_out3                    = 0x0000000b,
  regk_iop_spu_gio_out4                    = 0x0000000c,
  regk_iop_spu_gio_out5                    = 0x0000000d,
  regk_iop_spu_gio_out6                    = 0x0000000e,
  regk_iop_spu_gio_out7                    = 0x0000000f,
  regk_iop_spu_n                           = 0x00000003,
  regk_iop_spu_no                          = 0x00000000,
  regk_iop_spu_r0                          = 0x00000008,
  regk_iop_spu_r1                          = 0x00000009,
  regk_iop_spu_r10                         = 0x0000000a,
  regk_iop_spu_r11                         = 0x0000000b,
  regk_iop_spu_r12                         = 0x0000000c,
  regk_iop_spu_r13                         = 0x0000000d,
  regk_iop_spu_r14                         = 0x0000000e,
  regk_iop_spu_r15                         = 0x0000000f,
  regk_iop_spu_r2                          = 0x0000000a,
  regk_iop_spu_r3                          = 0x0000000b,
  regk_iop_spu_r4                          = 0x0000000c,
  regk_iop_spu_r5                          = 0x0000000d,
  regk_iop_spu_r6                          = 0x0000000e,
  regk_iop_spu_r7                          = 0x0000000f,
  regk_iop_spu_r8                          = 0x00000008,
  regk_iop_spu_r9                          = 0x00000009,
  regk_iop_spu_reg_hi                      = 0x00000002,
  regk_iop_spu_reg_lo                      = 0x00000002,
  regk_iop_spu_rw_brp_default              = 0x00000000,
  regk_iop_spu_rw_brp_size                 = 0x00000004,
  regk_iop_spu_rw_ctrl_default             = 0x00000000,
  regk_iop_spu_rw_event_cfg_size           = 0x00000004,
  regk_iop_spu_rw_event_mask_size          = 0x00000004,
  regk_iop_spu_rw_event_val_size           = 0x00000004,
  regk_iop_spu_rw_gio_out_default          = 0x00000000,
  regk_iop_spu_rw_r_size                   = 0x00000010,
  regk_iop_spu_rw_reg_access_default       = 0x00000000,
  regk_iop_spu_stat_in                     = 0x00000002,
  regk_iop_spu_statin_hi                   = 0x00000004,
  regk_iop_spu_statin_lo                   = 0x00000004,
  regk_iop_spu_trig                        = 0x00000003,
  regk_iop_spu_trigger                     = 0x00000006,
  regk_iop_spu_v                           = 0x00000001,
  regk_iop_spu_wsts_gioout_spec            = 0x00000001,
  regk_iop_spu_xor                         = 0x00000003,
  regk_iop_spu_xor_bus0_r2_0               = 0x00000000,
  regk_iop_spu_xor_bus0m_r2_0              = 0x00000002,
  regk_iop_spu_xor_bus1_r3_0               = 0x00000001,
  regk_iop_spu_xor_bus1m_r3_0              = 0x00000003,
  regk_iop_spu_yes                         = 0x00000001,
  regk_iop_spu_z                           = 0x00000002
};
#endif /* __iop_spu_defs_h */

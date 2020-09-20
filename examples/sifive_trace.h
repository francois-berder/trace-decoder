/* Copyright 2020 SiFive, Inc */

#ifndef SIFIVE_TRACE_H_
#define SIFIVE_TRACE_H_

#define traceBaseAddress       (0x10000000)
#define te_control_offset      (0x00)
#define te_impl_offset         (0x04)
#define te_sinkbase_offset     (0x10)
#define te_sinkbasehigh_offset (0x14)
#define te_sinklimit_offset    (0x18)
#define te_sink_wp_offset      (0x1c)
#define te_sink_rp_offset      (0x20)
#define te_fifo_offset         (0x30)
#define te_btmcount_offset     (0x34)
#define te_wordcount_offset    (0x38)
#define ts_control_offset      (0x40)
#define ts_lower_offset        (0x44)
#define ts_upper_offset        (0x48)
#define xti_control_offset     (0x50)
#define xto_control_offset     (0x54)
#define wp_control_offset      (0x58)
#define itc_traceenable_offset (0x60)
#define itc_trigenable_offset  (0x64)

#define	caBaseAddress       (0x1000f000)
#define ca_control_offset   (0x00)
#define ca_impl_offset      (0x04)
#define ca_sink_wp_offset   (0x1c)
#define ca_sink_rp_offset   (0x20)
#define ca_sink_data_offset (0x24)

#define mww(addr,data) (*(unsigned int*)(addr)=(data))
#define mrw(addr) (*(unsigned int*)(addr))

// caTraceOn selects SRAM for both traces, stop on wrap for both traces

#define caTraceOn() {                                    \
    mww(traceBaseAddress+te_control_offset, 0x00000000); \
    mww(caBaseAddress+ca_control_offset, 0x00000000);    \
    mww(traceBaseAddress+te_control_offset, 0x00000001); \
    mww(traceBaseAddress+te_sink_wp_offset, 0x00000000); \
    mww(traceBaseAddress+te_sink_rp_offset, 0x00000000); \
    mww(traceBaseAddress+xti_control_offset,0x04);       \
    mww(traceBaseAddress+xto_control_offset,0x21);       \
    mww(caBaseAddress+ca_control_offset,0x01);           \
    mww(caBaseAddress+ca_sink_wp_offset,0x00000000);     \
    mww(caBaseAddress+ca_sink_rp_offset,0x00000000);     \
    mww(caBaseAddress+ca_control_offset,0x40004003);     \
    mww(traceBaseAddress+te_control_offset,0x4153c077); }

#define caTraceOff() {                                                 \
    mww(traceBaseAddress+te_control_offset, 0x00000001);               \
    mww(caBaseAddress+ca_control_offset, 0x00000001);                  \
    while ((mrw(caBaseAddress+ca_control_offset) & 0x00000008) == 0) {} \
    while ((mrw(caBaseAddress+ca_control_offset) & 0x00000008) == 0) {} }

#endif // SIFIVE_TRACE_H_

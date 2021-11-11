#
# Scripts for trace using OpenOCD
#

set te_control_offset      0x00
set te_impl_offset         0x04
set ev_control_offset      0x0C
set te_sinkbase_offset     0x10
set te_sinkbasehigh_offset 0x14
set te_sinklimit_offset    0x18
set te_sinkwp_offset       0x1c
set te_sinkrp_offset       0x20
set te_sinkdata_offset     0x24
set te_fifo_offset         0x30
set te_btmcount_offset     0x34
set te_wordcount_offset    0x38
set ts_control_offset      0x40
set ts_lower_offset        0x44
set ts_upper_offset        0x48
set xti_control_offset     0x50
set xto_control_offset     0x54
set wp_control_offset      0x58
set itc_traceenable_offset 0x60
set itc_trigenable_offset  0x64
set pib_control_offset     0xf00

set ca_control_offset   0x00
set ca_impl_offset      0x04
set ca_sink_wp_offset   0x1c
set ca_sink_rp_offset   0x20
set ca_sink_data_offset 0x24

set pcs_control_offset  0x100
set pcs_capture         0x13c
set pcs_capture_hi      0x138
set pcs_sample          0x17c
set pcs_sample_hi       0x178

set num_cores  0
set num_funnels 0
set have_htm 0
set has_event 0

set trace_buffer_width 0

#set traceBufferAddr 0x00000000
set trace_buffer_width 0

#set traceBaseAddresses { 0x10000000 }
#set traceFunnelAddresses { 0 }

set verbose 0

set fs_enable_trace "on"

# local helper functions not intended to be called directly

proc wordhex {addr} {
    mem2array x 32 $addr 1
    return [format "0x%08x" [lindex $x 1]]
}

proc word {addr} {
    mem2array x 32 $addr 1
    return [lindex $x 1]
}

proc setAllTeControls {offset val} {
    global traceBaseAddresses

#    echo "setAllTeControls([format 0x%08lx $offset] [format 0x%08lx $val])"

    foreach controlReg $traceBaseAddresses {
        mww [expr $controlReg + $offset] $val
    }
}

proc setAllTfControls {offset val} {
    global traceFunnelAddresses

#    echo "setAllTfControls([format 0x%08lx $offset] [format 0x%08lx $val])"

    if {($traceFunnelAddresses != 0) && ($traceFunnelAddresses != "")} {
    	foreach controlReg $traceFunnelAddresses {
        	mww [expr $controlReg + $offset] $val
        }
    }
}

# Returns list of all cores and funnel if present

proc getAllCoreFunnelList {} {
    global traceBaseAddresses
    global traceFunnelAddresses

    set cores {}
    set index 0

    foreach controlReg $traceBaseAddresses {
        lappend cores $index
        set index [expr $index + 1]
    }

    if {($traceFunnelAddresses != 0) && ($traceFunnelAddresses != "")} {
        lappend cores funnel
    }

    return $cores
}

# Returns list of all cores (but not funnel)

proc getAllCoreList {} {
    global traceBaseAddresses

    set cores {}
    set index 0

    foreach controlReg $traceBaseAddresses {
        lappend cores $index
        incr index
    }

    return $cores
}

# returns a list struct from parsing $cores with each element a core id

proc parseCoreFunnelList {cores} {
    global num_cores
    global num_funnels

    # parse core and build list of cores

    if {$cores == "all" || $cores == ""} {
        return [getAllCoreFunnelList]
    }

    set t [split $cores ","]

    foreach core $t {
        if {$core == "funnel"} {
            # only accept funnel if one is present

            if {$num_funnels == 0} {
                return "error"
            }
        } elseif {$core < 0 || $core > $num_cores} {
            return "error"
        }
    }

    # t is now a list of cores

    return $t
}

proc parseCoreList {cores} {
    global num_cores

    # parse core and build list of cores

    if {$cores == "all" || $cores == ""} {
        return [getAllCoreList]
    }

    set t [split $cores ","]

    foreach core $t {
        if {($core < 0 || $core >= $num_cores)} {
            return "error"
        }
    }

    # t is now a list of cores

    return $t
}

proc cores {} {
    return [parseCoreFunnelList "all"]
}

# funnel routines for multi-core. Not indented to be called directly!

proc getSinkBits {} {
    global traceBaseAddrArray
    global te_impl_offset
    global num_cores
    global num_funnels
    
    if {$num_funnels > 0} {
        set sink [word [expr $traceBaseAddrArray(funnel) + $te_impl_offset]]
        set hexSink [format "0x%08x" [expr $sink & 0x1f0]]
        return $hexSink
    }
    
    if {$num_cores > 0} {
        set sink [word [expr $traceBaseAddrArray(0) + $te_impl_offset]]
        set hexSink [format "0x%08x" [expr $sink & 0x1f0]]
        return $hexSink
    }
    
    return 0x0
}

proc hasFunnelSink { addr } {
	global te_impl_offset

    set impl [word [expr $addr + $te_impl_offset]]
    if {($impl & (1 << 8))} {
        return 1
    }
    
    return 0
}

proc setFunnelSink { addr sink } {
    global te_impl_offset
    global te_control_offset

# make sure sink is supported by funnel

#echo "setFunnelSink(): addr: $addr sink: $sink"

    switch [string toupper $sink] {
    "SRAM"   { set checkbit 4
               set sinkmask 4 }
    "SBA"    { set checkbit 7
               set sinkmask 7 }
    "ATB"    { set checkbit 5
               set sinkmask 5 }
    "PIB"    { set checkbit 6
               set sinkmask 6 }
    "FUNNEL" { set checkbit 8
               set sinkmask 8 }
    default  { set sinkmask 0 }
    }

    if {$sinkmask == 0} {
        return 1
    }
    
    set impl [word [expr $addr + $te_impl_offset]]

    if {($impl & (1 << $checkbit)) != 0} {
        set control [word [expr $addr + $te_control_offset]]
        set control [expr $control & ~(0x0f << 28)]
        set control [expr $control | ($sinkmask << 28)]
        mww [expr $addr + $te_control_offset] $control

        return 0
    }
    
    return 1
}

proc setFunnelSinks { sink } {
    global traceBaseAddrArray
    global traceFunnelAddrArray
    global num_funnels

    if {$num_funnels > 0} {
        for {set i 0} {$i < ($num_funnels - 1)} {incr i} {
            setFunnelSink $traceFunnelAddrArray($i) "funnel"
        }
    
        setFunnelSink $traceBaseAddrArray(funnel) "SRAM"
    }
    
    return $num_funnels
}

# end multi-core funnel routines

proc checkHaveHTM {} {
    global traceBaseAddresses
    global te_control_offset
    global verbose

#    echo "checkHaveHTM()"

    set baseAddress [lindex $traceBaseAddresses 0]
    set tracectl [word [expr $baseAddress + $te_control_offset]]
    set saved $tracectl
    set tracectl [expr $tracectl & 0xffffff8f]
    set tracectl [expr $tracectl | 0x00000070]
    mww [expr $baseAddress + $te_control_offset] $tracectl
    set tmp [word [expr $baseAddress + $te_control_offset]]

    # restore te_control

    mww [expr $baseAddress + $te_control_offset] $saved

    if {(($tmp & 0x00000070) >> 4) == 0x7} {
        if {$verbose > 0} {
            echo "supports htm"
        }
        return 1
    }

    if {$verbose > 0} {
        echo "does not support htm"
    }

    return 0
}

proc checkHaveEvent {} {
    global traceBaseAddresses
    global ev_control_offset
    global verbose

#    echo "checkHaveEvent()"

    set baseAddress [lindex $traceBaseAddresses 0]
    set evctl [word [expr $baseAddress + $ev_control_offset]]
    set saved $evctl
    set evctl 0x03f
    mww [expr $baseAddress + $ev_control_offset] $evctl
    set evctl [word [expr $baseAddress + $ev_control_offset]]

    # restore ev_control

    mww [expr $baseAddress + $ev_control_offset] $saved

    if {$evctl != 0} {
        if {$verbose > 0} {
            echo "supports event"
        }
        return 1
    }

    if {$verbose > 0} {
        echo "does not support event"
    }

    return 0
}

# ite = [i]s [t]race [e]nabled
proc ite {} {
    global te_control_offset
    global traceBaseAddresses
    global traceBaseAddrArray
    global num_funnels

    set rc 0

#    echo "ite()"

    if {$num_funnels > 0} {
        set tracectl [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        if {($tracectl & 0x2) == 0} {
            return 0
        }
    }

    foreach baseAddress $traceBaseAddresses {
        set tracectl [word [expr $baseAddress + $te_control_offset]]
        if {($tracectl & 0x6) != 0} {
            return 1
        }
    }

    return 0
}

proc setTraceBufferWidth {} {
    global traceBaseAddrArray
    global te_impl_offset
    global te_sinkbase_offset
    global num_funnels
    global trace_buffer_width

#    echo "setTraceBufferWidth()"

    if {$num_funnels > 0} {
        set impl [word [expr $traceBaseAddrArray(funnel) + $te_impl_offset]]
        if {($impl & (1 << 7))} {
            set t [word [expr $traceBaseAddrArray(funnel) + $te_sinkbase_offset]]
            mww [expr $traceBaseAddrArray(funnel) + $te_sinkbase_offset] 0xffffffff
            set w [word [expr $traceBaseAddrArray(funnel) + $te_sinkbase_offset]]
            mww [expr $traceBaseAddrArray(funnel) + $te_sinkbase_offset] $t

            if {$w == 0} {
            set trace_buffer_width 0
            return 0
            }

            for {set i 0} {($w & (1 << $i)) == 0} {incr i} { }

            set trace_buffer_width [expr 1 << $i]
            return $trace_buffer_width
        }
    }

    set impl [word [expr $traceBaseAddrArray(0) + $te_impl_offset]]
    if {($impl & (1 << 7))} {
        set t [word [expr $traceBaseAddrArray(0) + $te_sinkbase_offset]]
        mww [expr $traceBaseAddrArray(0) + $te_sinkbase_offset] 0xffffffff
        set w [word [expr $traceBaseAddrArray(0) + $te_sinkbase_offset]]
        mww [expr $traceBaseAddrArray(0) + $te_sinkbase_offset] $t

        if {$w == 0} {
            set trace_buffer_width 0
            return 0
        }

        for {set i 0} {($w & (1 << $i)) == 0} {incr i} { }

        set trace_buffer_width [expr 1 << $i]
        return $trace_buffer_width
    }

    set trace_buffer_width 0
    return 0
}

proc getTraceEnable {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getTraceEnable($core)"

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {($tracectl & 0x2) != 0} {
        return "on"
    }

    return "off"
}
proc getSBABaseAddress {core} {
    global traceBaseAddrArray
    global te_sinkbase_offset
    global te_sinkbasehigh_offset
    global verbose

#    echo "getSBABaseAddress($core)"

    set tracebase [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
    set tracebasehi [word [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]

    set baseAddr [expr $tracebase + ($tracebasehi << 32)]
    
    if { $verbose > 1 } {
        echo "getSBABaseAddress: [format 0x%016lx $baseAddr]"
    }

    return $baseAddr
}

proc getTracingEnable {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getTracingEnable($core)"

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {($tracectl & 0x4) != 0} {
        return "on"
    }

    return "off"
}

proc manualtrace {{f "help"}} {
    global fs_enable_trace

#    echo "manualtrace($f)"

    if {$f == "on"} {
        set fs_enable_trace "off"
    } elseif {$f == "off"} {
        set fs_enable_trace "on"
    } elseif {$f == "help"} {
        if {$fs_enable_trace == "on"} {
            echo "Manual trace is off"
        } elseif {$fs_enable_trace == "off"} {
            echo "Manual trace is on"
        } else {
            echo "Bad value for fs_enable_trace: $fs_enable_trace"
        }
    } else {
        echo "Error: usage: manualtrace [on | off]"
    }

    echo -n ""
}
proc clearAndEnableTrace { core } {

#    echo "clearAndEnableTrace($core)"

    cleartrace $core
    enableTraceEncoder $core
}

proc enableTraceEncoder {core} {
    global fs_enable_trace

#    echo "enableTraceEncoder($core)"

    if {$fs_enable_trace == "on"} {
        enableTraceEncoderManual $core
    }
}

proc enableTraceEncoderManual {core} {
    global traceBaseAddrArray
    global te_control_offset

#  echo "enableTraceEncoderManual($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t | 0x00000003]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc enableTracing {core} {
    global fs_enable_trace

 #  echo "enableTraceing($core)"

    if {$fs_enable_trace == "on"} {
        enableTracingManual $core
    }
}

proc enableTracingManual {core} {
    global traceBaseAddrArray
    global te_control_offset
    global fs_enable_trace
    global num_funnels
    global traceFunnelAddresses

#  echo "enableTraceingManual($core)"

    if {$num_funnels > 0} {
        foreach funnel $traceFunnelAddresses {
            set t [word [expr $funnel + $te_control_offset]]
            set t [expr $t | 0x00000003]
            mww [expr $funnel + $te_control_offset] $t
        }
    }
    
    if {$core != "funnel"} {
        set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
        set t [expr $t | 0x00000005]
        mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
    }
}

proc disableTraceEncoder {core} {
    global fs_enable_trace

#    echo "disableTraceEncoder($core)"

    if {$fs_enable_trace == "on"} {
        disableTraceEncoderManual $core
    }
}

proc disableTraceEncoderManual {core} {
    global traceBaseAddresses
    global traceBaseAddrArray
    global te_control_offset
    global fs_enable_trace
    global traceFunnelAddrArray
    global num_funnels

#    echo "disableTraceEncoderNManual($core)"

    # need to flush all cores and funnel, not just $core

    set flushFunnel "false"
    set flushPIB "false"
    set flushATB "false"

    set currentCore 0
    
    foreach controlReg $traceBaseAddresses {
        set te [word [expr $controlReg + $te_control_offset]]
        set t [expr $te & ~0x00000004]
        set t [expr $t | 0x00000001]

        # clear teTracing, make sure not in reset
        mww [expr $controlReg + $te_control_offset] $t

        # clear teEnable
        set t [expr $t & ~0x00000002]
        mww [expr $controlReg + $te_control_offset] $t

        # need to poll teEmpty here until it reads 1

        set t [word [expr $controlReg + $te_control_offset]]
        while {($t & 0x00000008) == 0} {
            set t [word [expr $controlReg + $te_control_offset]]
            incr c
        }

        if {$currentCore != $core} {
            # restore teControl
            mww [expr $controlReg + $te_control_offset] $te
        }

        incr currentCore

        set sink [expr ($te >> 28) & 0x0f]

        switch $sink {
        5 { set flushATB "true"    }
        6 { set flushPIB "true"    }
        8 { set flushFunnel "true" }
        }
    }

    if {($flushFunnel != "false") && ($num_funnels > 0)} {
       	# if multi-core, flush all slave funnels first
   	    for {set f 0} { $f < ($num_funnels - 1)} {incr f} {
            set tf [word [expr $traceFunnelAddrArray($f) + $te_control_offset]]
            set t [expr $tf & ~0x00000004]
            set t [expr $t | 0x00000001]

            # clear teTracing, make sure not in reset
            mww [expr $traceFunnelAddrArray($f) + $te_control_offset] $t
            set t [expr $t & ~0x00000002]

            # clear teEnable
            mww [expr $traceFunnelAddrArray($f) + $te_control_offset] $t

            # need to poll teEmpty here until it reads 1
            set t [word [expr $traceFunnelAddrArray($f) + $te_control_offset]]
            while {($t & 0x00000008) == 0} {
                set t [word [expr $traceFunnelAddrArray($f) + $te_control_offset]]
            }

            # restore tfControl
            mww [expr $traceFunnelAddrArray($f) + $te_control_offset] $tf

            set sink [expr ($tf >> 28) & 0x0f]

            switch $sink {
            5 { set flushATB true    }
            6 { set flushPIB true    }
            }
        }
        
        # now flush master funnel

        set tf [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        set t [expr $tf & ~0x00000004]
        set t [expr $t | 0x00000001]

        # clear teTracing, make sure not in reset
        mww [expr $traceBaseAddrArray(funnel) + $te_control_offset] $t
        set t [expr $t & ~0x00000002]

        # clear teEnable
        mww [expr $traceBaseAddrArray(funnel) + $te_control_offset] $t

        # need to poll teEmpty here until it reads 1

        set t [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        while {($t & 0x00000008) == 0} {
            set t [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        }

        if {$core != "funnel"} {
            # restore tfControl
            mww [expr $traceBaseAddrArray(funnel) + $te_control_offset] $tf
        }

        set sink [expr ($tf >> 28) & 0x0f]

        switch $sink {
        5 { set flushATB true    }
        6 { set flushPIB true    }
        }
    }

    if {$flushPIB != "false"} {
        # flesh this out later
    }

    if {$flushATB != "false"} {
        # flesh this out later
    }
}

proc disableTracing {core} {
    global traceBaseAddrArray
    global te_control_offset
    global fs_enable_trace

#    echo "disableTracing($core)"

    if {$fs_enable_trace == "on"} {
        set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
        set t [expr $t & ~0x00000004]
        set t [expr $t | 0x00000001]
        mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
    }
}

proc resetTrace {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "resetTrace($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] 0
    set t [expr $t | 0x00000001]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getSinkError {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getSinkError($core)"

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {(($tracectl >> 27) & 1) != 0} {
        return "Error"
    }

    return "Ok"
}

proc clearSinkError {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "clearSinkError($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t | (1 << 27)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc isTsEnabled {core} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

    if {$core != "funnel"} {
        set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
        
        if {($tsctl & 0x00008001) != 0x00008001} {
            return "off"
        }

        switch [expr ($tsctl >> 4) & 0x07] {
        0  { return "off" }
        1  { return "on"  }
        2  { if {($tsctl & 0x02) == 0x02} {
                 return "on"
             }
             return "off"
           }
        3  { if {($tsctl & 0x02) == 0x02} {
                 return "on"
             }
             return "off"
           }
        }

        # must have a slave TS unit - check if master is on
        
        if {$num_funnels > 1} {
            # check master funnel
            set tsctl [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        } else {
            # check core 0
            set tsctl [word [expr $traceBaseAddrArray(0) + $ts_control_offset]]
        }
        
        if {($tsctl & 0x03) == 0x03} {
            return "on"
        }
    } elseif {$num_funnels > 1} {
        # check if master funnel is on
        set tsctl [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        
        if {($tsctl & 0x 03) == 0x03} {
          return "on"
        }
    }
    
    return "off"
}

proc enableTs {core} {
    global traceBaseAddrArray
    global _offset
    global num_funnels
    global traceFunnelAddresses
    global ts_control_offset
    
#    echo "enableTs($core)"

    if {$core != "funnel"} {
        set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
        set tsctl [expr $tsctl | 0x00008003]
        mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
        
        if {(($tsctl >> 4) & 0x07) == 4} {
            #ts unit for core is a slave

            if {$num_funnels > 1} {
                foreach funnel $traceFunnelAddresses {
                    set tsctl [word [expr $funnel + $ts_control_offset]]
                    set tsctl [expr $tsctl | 0x00000003]
                    mww [expr $funnel + $ts_control_offset] $tsctl
                }
            } else {
                # master is in core 0 - enable it

                set tsctl [word [expr $traceBaseAddrArray(0) + $ts_control_offset]]
                set tsctl [expr $tsctl | 0x00000003]
                mww [expr $traceBaseAddrArray(0) + $ts_control_offset] $tsctl
            }
        }
    }
}

proc disableTs {core} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

#    echo "diableTs($core)"

#    if {$num_funnels > 1} {
#        # this may not be necessary
#        set tsctl [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
#        set tsctl [expr $tsctl & ~0x00000001]
#        mww [expr $traceBaseAddrArray(funnel) + $ts_control_offset] $tsctl
#    }
    
    if {$core != "funnel"} {
        set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
        set tsctl [expr $tsctl & ~0x00008000]
        mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
    }
}

proc resetTs {core} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

#    echo "resetTs($core)"

    if {$num_funnels > 1} {
        # If there is a master funnel, always reset its ts unit
        
        set tsctl [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        set tsctl [expr $tsctl | 0x00008004]
        mww [expr $traceBaseAddrArray(funnel) + $ts_control_offset] $tsctl
        set t [expr $tsctl & ~0x00008004]
        mww [expr $traceBaseAddrArray(funnel) + $ts_control_offset] $tsctl
    }

    if {$core != "funnel"} {        
        set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
        set tsctl [expr $tsctl | 0x00008004]
        mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
        set t [expr $tsctl & ~0x00008004]
        mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
    }
}

proc getTsDebug {core} {
    global traceBaseAddrArray
    global ts_control_offset
	global num_funnels
	
#    echo "getTsDebug($core)"

    if {$num_funnels > 1} {
        return "on"
    }
    
    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    if {[expr $tsctl & 0x00000008] != 0} {
        return "on"
    }

    return "off"
}

proc getTsLower {core} {
    global traceBaseAddrArray
    global ts_lower_offset

#    echo "getTsLower($core)"

    return [format 0x%08x [word [expr $traceBaseAddrArray($core) + $ts_lower_offset]]]
}

proc enableTsDebug {core} {
    global traceBaseAddrArray
    global ts_control_offset
	global num_funnels
	
#    echo "enableTsDebug($core)"

	if {$num_funnels > 1} {
	    return 1
	}

    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set tsctl [expr $tsctl | 0x0000008]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl

    return 0
}

proc disableTsDebug {core} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

#    echo "disableTsDebug($core)"

    if {$num_funnels > 1} {
        return 1
    }
    
    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set tsctl [expr $tsctl & ~0x0000008]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
    
    return 0
}

proc getTsClockSrc {core} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "getTsClockSrc($core)"

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr ($t >> 4) & 0x7]
    switch $t {
        0       { return "none"     }
        1       { return "external" }
        2       { return "bus"      }
        3       { return "core"     }
        4       { return "slave"    }
        default { return "reserved" }
    }
}

proc getTsPrescale {core} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

#    echo "getTsPrescale($core)"

    if {$num_funnels > 1} {
        # if multi-core, use master funnel and ignore core
            
        set t [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        set t [expr ($t >> 8) & 0x3]
        switch $t {
        0 { return 1  }
        1 { return 4  }
        2 { return 16 }
        3 { return 64 }
        }
    }
    
    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr ($t >> 8) & 0x3]
    switch $t {
        0 { return 1  }
        1 { return 4  }
        2 { return 16 }
        3 { return 64 }
    }
}

proc setTsPrescale {core prescl} {
    global traceBaseAddrArray
    global ts_control_offset
    global num_funnels

#    echo "setTsPrescale($core $prescl)"

    switch $prescl {
        1       { set ps 0 }
        4       { set ps 1 }
        16      { set ps 2 }
        64      { set ps 3 }
        default { set ps 0 }
    }

    # if multi-core, we ignore core, and just set master funnel

    if {$num_funnels > 1} {
        set t [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        set t [expr $t & ~0x0300]
        set t [expr $t | ($ps << 8)]
        mww [expr $traceBaseAddrArray(funnel) + $ts_control_offset] $t
    } else {
        set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
        set t [expr $t & ~0x0300]
        set t [expr $t | ($ps << 8)]
        mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
    }
}

proc getTsBranch {core} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "getTsBranch($core)"

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr ($t >> 16) & 0x3]
    switch $t {
        0 { return "off"  }
        1 { return "indirect+exception"  }
        2 { return "reserved" }
        3 { return "all" }
    }
}

proc setTsBranch {core branch} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "setTsBranch($core $branch)"

    switch $branch {
        "off"                { set br 0 }
        "indirect+exception" { set br 1 }
        "reserved"           { set br 2 }
        "all"                { set br 3 }
        default              { set br 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr $t & ~0x30000]
    set t [expr $t | ($br << 16)]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
}

proc setTsITC {core itc} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "setTsITC($core $itc)"

    switch $itc {
        "on"    { set f 1 }
        "off"   { set f 0 }
        default { set f 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr $t & ~0x40000]
    set t [expr $t | ($f << 18)]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
}

proc getTsITC {core} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "getTsITC($core)"

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr ($t >> 18) & 0x1]

    switch $t {
        0 { return "off"  }
        1 { return "on"  }
    }
}

proc setTsOwner {core owner} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "setTsOwner($core $owner)"

    switch $owner {
        "on"    { set f 1 }
        "off"   { set f 0 }
        default { set f 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr $t & ~0x80000]
    set t [expr $t | ($f << 19)]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
}

proc getTsOwner {core} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "getTsOwner($core)"

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr ($t >> 19) & 0x1]

    switch $t {
        0 { return "off"  }
        1 { return "on"  }
    }
}

proc setTeStopOnWrap {core wrap} {
    global traceBaseAddrArray
    global te_control_offset
    global num_funnels
    
#    echo "setTeStopOnWrap($core $wrap)"
	
    switch $wrap {
        "on"    { set sow 1 }
        "off"   { set sow 0 }
        default { set sow 0 }
    }

	if {$num_funnels > 0} {
        set t [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        set t [expr $t & ~0x4000]
        set t [expr $t | ($sow << 14)]
        mww [expr $traceBaseAddrArray(funnel) + $te_control_offset] $t
	} else {
        set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
        set t [expr $t & ~0x4000]
        set t [expr $t | ($sow << 14)]
        mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
    }
}

proc getTeStopOnWrap {core} {
    global traceBaseAddrArray
    global te_control_offset
    global num_funnels
    
#    echo "getTeStopOnWrap($core)"

    if {$num_funnels > 0} {
        set t [word [expr $traceBaseAddrArray(funnel) + $te_control_offset]]
        set t [expr ($t >> 14) & 0x1]
    } else {
        set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
        set t [expr ($t >> 14) & 0x1]
    }

    switch $t {
    0 { return "off"  }
    1 { return "on"  }
    }
}

proc getCAStopOnWrap {core} {
    global CABaseAddrArray
    global ca_control_offset

#    echo "getCAStopOnWrap($core)"

    set t [word [expr $CABaseAddrArray($core) + $ca_control_offset]]
    set t [expr ($t >> 14) & 0x1]

    switch $t {
    0 { return "off"  }
    1 { return "on"  }
    }
}

proc setTeStallEnable {core enable} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "setTeStallEnable($core $enable)"

    switch $enable {
        "on"    { set en 1 }
        "off"   { set en 0 }
        default { set en 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~(1 << 13)]
    set t [expr $t | ($en << 13)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getTeStallEnable {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getTeStallEnable($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 13) & 0x1]

    switch $t {
        0 { return "off"  }
        1 { return "on"  }
    }
}

proc setTraceMode { core usermode } {
    global have_htm

#    echo "setTraceMode($core $usermode)"

    switch $usermode {
        "off" 
        {
            setTargetTraceMode $core "none"
        }
        "instruction" 
        {
            if {$have_htm == 1} {
                setTargetTraceMode $core "htm+sync"
            } else {
                setTargetTraceMode $core "btm+sync"
            }
        }
        "sample" 
        {
            setTargetTraceMode $core "sample"
        }
        "event"
        {
            setTargetTraceMode $core "event"
        }
    }
}

proc setTargetTraceMode {core mode} {
    global traceBaseAddrArray
    global te_control_offset
    global has_event
    global have_htm

#    echo "setTargetTraceMode($core $mode)"

    switch $mode {
       "none"       { set tm 0 }
       "sample"     { set tm 1
	              setEventControl $core "sample" }
       "event"      { set tm 1 }
       "btm+sync"   { set tm 3 }
       "btm"        { set tm 3 }
       "htmc+sync"  { set tm 6 }
       "htmc"       { set tm 6 }
       "htm+sync"   { set tm 7 }
       "htm"        { set tm 7 }
       "all"        { if {$have_htm == 1} {
                          set tm 7
                      } else {
                          set tm 3
                      }
                    }
       default      { set tm 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x0070]
    set t [expr $t | ($tm << 4)]

    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getTraceMode {core} {
    set tm [getTargetTraceMode $core]

#	echo "getTraceMode($core)"

    switch $tm {
       "none"       { return "off" }
       "sample"     { return "sample" }
       "event"      { return "event" }
       "btm+sync"   { return "instruction" }
       "htmc+sync"  { return "instruction" }
       "htm+sync"   { return "instruction" }
    }
    return "off"
}

proc getTargetTraceMode {core} {
    global traceBaseAddrArray
    global te_control_offset
    global ev_control_offset
    global has_event

#    echo "getTargetTraceMode($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 4) & 0x7]

    switch $t {
       0       { return "none" }
       1       { if {[word [expr $traceBaseAddrArray($core) + $ev_control_offset]] == 0} {
		       if {[getMaxIcnt $core] == 15} {
			       return "none"
		       }
                    return "sample"
                 } else {
                    return "event"
                 }
               }
       3       { return "btm+sync"  }
       6       { return "htmc+sync" }
       7       { return "htm+sync"  }
       default { return "reserved" }
    }
}

proc getEventControl {core} {
    global traceBaseAddrArray
    global ev_control_offset
    global has_event

    if {$has_event== 0} {
        if {[getMaxIcnt $core] == 15} {
            return "none"
        }

        return "sample"
    }

    if {[getMaxIcnt $core] == 15} {
        set eventl ""
    } else {
        set eventl "sample"
    }

    set events [word [expr $traceBaseAddrArray($core) + $ev_control_offset]]

    if {($events & (1 << 0)) != 0} {
        append eventl " trigger"
    }

    if {($events & (1 << 1)) != 0} {
        append eventl " watchpoint"
    }

    if {($events & (1 << 2)) != 0} {
        append eventl " call"
    }

    if {($events & (1 << 3)) != 0} {
        append eventl " interrupt"
    }

    if {($events & (1 << 4)) != 0} {
        append eventl " exception"
    }

    if {($events & (1 << 5)) != 0} {
        append eventl " context"
    }

    if {$eventl == ""} {
        return "none"
    }

    return $eventl
}

proc setEventControl {core opts} {
    global traceBaseAddrArray
    global ev_control_offset
    global has_event

    if {$has_event == 0} {
        return 1
    }

    set reg 0
    set icnt 15

    foreach opt $opts {
        switch $opt {
        "none"       { set reg 0
		       set icnt 15 }
        "all"        { set reg 0x3f
                       set icnt 0}
	"sample"     { set icnt 0 }
        "trigger"    { set reg [expr $reg | (1 << 0)] }
        "watchpoint" { set reg [expr $reg | (1 << 1)] }
        "call"       { set reg [expr $reg | (1 << 2)] }
        "interrupt"  { set reg [expr $reg | (1 << 3)] }
        "exception"  { set reg [expr $reg | (1 << 4)] }
        "context"    { set reg [expr $reg | (1 << 5)] }
        default      { return 1 }
        }
    }

    mww [expr $traceBaseAddrArray($core) + $ev_control_offset] $reg
    setMaxIcnt $core $icnt

    return 0
}

# Individual access to each event control bit works better in the case of
# certain UI environment(s) that may be accessing this script.  These routines are
# roughly equivalent to getEventControl and setEventControl, except they access each
# event bit individually, and also don't affect the maxIcnt setting.  Some environments
# will prefer to use getEventControl and setEventControl instead.
proc setEventControlBit { core bit enable } {
    global traceBaseAddrArray
    global ev_control_offset
    global has_event

    if {$has_event == 0} {
        return 1
    }
    set events [word [expr $traceBaseAddrArray($core) + $ev_control_offset]]
    if {$enable} {
	set events [expr $events | (1 << $bit)]	
    } else {
	set events [expr $events & ~(1 << $bit)]		
    }

    mww [expr $traceBaseAddrArray($core) + $ev_control_offset] $events

    return 0
}

proc getEventControlBit { core bit } {
    global traceBaseAddrArray
    global ev_control_offset
    global has_event

    if {$has_event == 0} {
        return 0
    }
    set events [word [expr $traceBaseAddrArray($core) + $ev_control_offset]]
    return [expr {($events & (1 << $bit)) != 0}]
}


proc setEventControlTrigger { core enable } {
    return [setEventControlBit $core 0 $enable]
}

proc getEventControlTrigger { core } {
    return [getEventControlBit $core 0]
}

proc setEventControlWatchpoint { core enable } {
    return [setEventControlBit $core 1 $enable]    
}

proc getEventControlWatchpoint { core } {
    return [getEventControlBit $core 1]
}

proc setEventControlCall { core enable } {
    return [setEventControlBit $core 2 $enable]    
}

proc getEventControlCall { core } {
    return [getEventControlBit $core 2]
}

proc setEventControlInterrupt { core enable } {
    return [setEventControlBit $core 3 $enable]    
}

proc getEventControlInterrupt { core } {
    return [getEventControlBit $core 3]
}

proc setEventControlException { core enable } {
    return [setEventControlBit $core 4 $enable]    
}

proc getEventControlException { core } {
    return [getEventControlBit $core 4]
}

proc setEventControlContext { core enable } {
    return [setEventControlBit $core 5 $enable]    
}

proc getEventControlContext { core } {
    return [getEventControlBit $core 5]
}

# END of routines that facilitate individual access to each event control bit 

proc setITC {core mode} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "setITC($core $mode)"

    switch $mode {
        "off"           { set itc 0 }
        "all"           { set itc 1 }
        "ownership"     { set itc 2 }
        "all+ownership" { set itc 3 }
        default         { set itc 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x0180]
    set t [expr $t | ($itc << 7)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getITC {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getITC($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 7) & 0x3]

    switch $t {
        0       { return "none" }
        1       { return "all"  }
        2       { return "ownership" }
        3       { return "all+ownership" }
        default { return "reserved" }
    }
}

proc setITCMask {core mask} {
    global traceBaseAddrArray
    global itc_traceenable_offset

#    echo "setITCMask($core $mask)"

    mww [expr $traceBaseAddrArray($core) + $itc_traceenable_offset] [expr $mask]
}

proc getITCMask {core} {
    global traceBaseAddrArray
    global itc_traceenable_offset

#    echo "getITCMask($core)"

    set t [wordhex [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]

    return $t
}

proc setITCTriggerMask {core mask} {
    global traceBaseAddrArray
    global itc_trigenable_offset

#    echo "setITCTriggerMask($core $mask)"

    mww [expr $traceBaseAddrArray($core) + $itc_trigenable_offset] [expr $mask]
}

proc getITCTriggerMask {core} {
    global traceBaseAddrArray
    global itc_trigenable_offset

#    echo "getITCTriggerMask($core)"

    set t [wordhex [expr $traceBaseAddrArray($core) + $itc_trigenable_offset]]

    return $t
}

proc setMaxIcnt {core maxicnt} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "setMaxIcnt($core $maxicnt)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0xf00000]
    set t [expr $t | ($maxicnt << 20)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getMaxIcnt {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getMaxIcnt($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & 0xf00000]
    set t [expr $t >> 20]

    return $t
}

proc findMaxICntFrom { core maxidx }  {
#	echo "findMaxIcntFrom($core $maxidx)"

    # We used to always start from 15, and assume the first sticking value
    # was the end of a contiguous range of valid indexes, but that is no longer
    # always true with latest encoders that can support event trace, so this is
    # a new proc that allows a maxidx to be passed in.
    # E.g. code can call with maxidx of 15 to find out if event trace is supported,
    # then can call again with maxidx of 14 to find out the largest value in the
    # valid contiguous range of values

    # Save current value so we can restore it.  Otherwise this
    # proc is destructive to the value.
    set original [getMaxIcnt $core]
    
    # Start on $maxidx and work down until one sticks.
    for {set x $maxidx} { $x > 0 } {set x [expr {$x - 1}]} {
        setMaxIcnt $core $x
        set y [getMaxIcnt $core]
        if {$x == $y} {
	    # restore original value before returning result
	    setMaxIcnt $core $original
            return $x;
        }
    }
}


proc findMaxICnt { core }  {
    #	echo "findMaxIcnt($core)"

    # Backward compatible shim that assumes 15 as the maxidx
    return [findMaxICntFrom $core 15]
}

proc setMaxBTM {core maxicnt} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "setMaxBTM($core $masicnt)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x0f0000]
    set t [expr $t | ($maxicnt << 16)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getMaxBTM {core} {
    global traceBaseAddrArray
    global te_control_offset

#    echo "getMaxBTM($core)"

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & 0x0f0000]
    set t [expr $t >> 16]

    return $t
}

proc setPibEnable {core bool_enable} {
    global traceBaseAddrArray
    global pib_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $pib_control_offset]]

    # activate if not already
    if { ($t & 0x1) == 0 } {
    set t [expr $t | 0x1]
    mww [expr $traceBaseAddrArray($core) + $pib_control_offset] $t	
    }
    
    if { $bool_enable } {
    set t [expr $t | 0x2]	
    } else {
    set t [expr $t & ~0x2]		
    }
    mww [expr $traceBaseAddrArray($core) + $pib_control_offset] $t
}

proc setPibMode {core mode} {
    global traceBaseAddrArray
    global pib_control_offset

    setPibEnable $core 1

    switch $mode {
        "off"               { set intmode 0 }
        "swt_manchester"    { set intmode 4 }
        "swt_uart"          { set intmode 5 }
        "tref_plus_1_tdata" { set intmode 8 }
        "tref_plus_1_tdata" { set intmode 9 }
        "tref_plus_1_tdata" { set intmode 10 }
        "tref_plus_1_tdata" { set intmode 11 }				
        default             { set intmode 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $pib_control_offset]]
    set t [expr $t & ~0x000000F0]
    set t [expr $t | ($intmode << 4)]
    mww [expr $traceBaseAddrArray($core) + $pib_control_offset] $t
}

proc getPibMode {core} {
    global traceBaseAddrArray
    global pib_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $pib_control_offset]]
    set t [expr ($t >> 4) & 0xF]

    switch $t {
        0       { return "off" }
        4       { return "swt_manchester"  }
        5       { return "swt_uart" }
        8       { return "tref_plus_1_tdata" }
        9       { return "tref_plus_2_tdata" }
        10      { return "tref_plus_4_tdata" }
        11      { return "tref_plus_8_tdata" }		
        default { return "reserved" }
    }
}

proc getPibDivider {core} {
    global traceBaseAddrArray
    global pib_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $pib_control_offset]]
    set t [expr ($t >> 16) & 0x1FFF]

    return $t
}

proc setPibDivider {core val} {
    global traceBaseAddrArray
    global pib_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $pib_control_offset]]
    set t [expr $t & ~0x1FFF0000]    
    set t [expr $t | ((($val & 0x1FFF)) << 16)]
    mww [expr $traceBaseAddrArray($core) + $pib_control_offset] $t
}

# helper functions used during debugging ot script

proc srcbits {} {
    global te_impl_offset
    global traceBaseAddresses

#    echo "srcbits()"

    set numbits [expr [word [expr [lindex $traceBaseAddresses 0] + $te_impl_offset]] >> 24 & 7]
    return $numbits
}

# global functions intended to be called from command line or from freedom studio

proc printtracebaseaddresses {} {
    global traceBaseAddresses
    global traceFunnelAddresses
    global num_funnels

#    echo "printtracebaseaddresses()"

    set core 0

    foreach baseAddress $traceBaseAddresses {
        echo "core $core: trace block at $baseAddress"
        set core [expr $core + 1]
    }

    if {$numFunnels > 0} {
        foreach funnel $traceFunnelAddresses {
            echo "Funnel block at $funnel"
        }
    }

    echo -n ""
}

proc getTeVersion {core} {
    global te_impl_offset
    global traceBaseAddresses

#    echo "getTeVersion($core)"

    set version [expr [word [expr [lindex $traceBaseAddresses 0] + $te_impl_offset]] & 7]
    return $version
}

proc teversion {{cores "all"} {opt ""}} {
    global te_impl_offset
    global traceBaseAddresses

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: teversion [corelist] [help]]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tev "core $core: "

            lappend tev [getTeVersion $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tev
        }

        return $rv
    } elseif {$opt == "help"} {
        echo "teversion: display trace encoder version"
        echo {Usage: teversion [corelist] [help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is"
        echo "            equivalent to all"
        echo "  help:     Display this message"
        echo ""
        echo "teversion with no arguments will display the trace encoder version for all cores"
        echo ""
    } else {
        echo {Usage: teversion [corelist] [help]}
    }
}

proc ts {{cores "all"} {opt ""}} {
    global ts_control_offset

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: ts [corelist] [on | off | reset | help]]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tse "core $core: "

            lappend tse [isTsEnabled $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tse
        }

        return $rv
    } elseif {$opt == "help"} {
        echo "ts: set or display timestamp mode"
        echo {Usage: ts [corelist] [on | off | reset | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is"
        echo "            equivalent to all"
        echo "  on:       Enable timestamps in trace messages"
        echo "  off:      Disable timstamps in trace messages"
        echo "  reset:    Reset the internal timestamp to 0"
        echo "  help:     Display this message"
        echo ""
        echo "ts with no arguments will display the current status of timestamps (on or off)"
        echo ""
    } elseif {$opt == "on"} {
        # iterate through coreList and enable timestamps
        foreach core $coreList {
            enableTs $core
        }
        echo -n ""
    } elseif {$opt == "off"} {
        foreach core $coreList {
            disableTs $core
        }
        echo -n ""
    } elseif {$opt == "reset"} {
        foreach core $coreList {
            resetTs $core
        }
        echo "timestamp reset"
    } else {
        echo {Error: Usage: ts [corelist] [on | off | reset | help]]}
    }
}

proc tsdebug {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsdebug [corelist] [on | off | reset | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsDebug $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tsd
        }

        return $rv
    }

    if {$opt == "help"} {
        echo "tsdebug: set or display if timestamp internal clock runs while in debug"
        echo {Usage: tsdebug [corelist] [on | off | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is"
        echo "            equivalent to all"
        echo "  on:       Timestamp clock continues to run while in debug"
        echo "  off:      Timnestamp clock halts while in debug"
        echo "  help:     Display this message"
        echo ""
        echo "tsdebug with no arguments will display the current status of timstamp debug"
        echo "(on or off)"
        echo ""
    } elseif {$opt == "on"} {
        # iterate through coreList and enable timestamps
        foreach core $coreList {
            enableTsDebug $core
        }
        echo -n ""
    } elseif {$opt == "off"} {
        foreach core $coreList {
            disableTsDebug $core
        }
        echo -n ""
    } else {
        echo {Error: Usage: tsdebug [corelist] [on | off | help]}
    }
}

proc tsclock {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsclock [corelist] [help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsClockSrc $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "tsclock: display the source of the timestamp clock (none, external, bus, core, or slave)"
        echo {Usage: tsclock [corelist] [none | external | bus | core | slave | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all."
        echo "  none:     No source for the timestampe clock"
        echo "  internal: Set the source of the timestamp clock to internal"
        echo "  external: Set the srouce of the timestamp clock to external"
        echo "  help:     Display this message"
        echo ""
    } else {
        echo {Error: Usage: tsclock [corelist] [help]}
    }
}

proc tsprescale {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsprescale [corelist] [1 | 4 | 16 | 64 | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsPrescale $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "tsprescale: set or display the timesampe clock prescalser (1, 4, 16, or 64)"
        echo {Usage: tsprescale [corelist] [1 | 4 | 16 | 64 | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "   1:       Set the prescaler to 1"
        echo "   4:       Set the prescaler to 4"
        echo "  16:       Set the prescaler to 16"
        echo "  64:       Set the prescaler to 64"
        echo "  help:     Display this message"
        echo ""
        echo "tspresacle with no arguments will display the current timestamp clock prescaler value (1, 4, 16, or 64)"
        echo ""
    } elseif {($opt == 1) || ($opt == 4) || ($opt == 16) || ($opt == 64)} {
        foreach core $coreList {
            setTsPrescale $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tsprescale [corelist] [1 | 4 | 16 | 64 | help]}
    }
}

proc tsbranch {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsbranch [coreslist] [off | indirect | all | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsBranch $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "tsbranch: set or display if timestamps are generated for branch messages"
        echo {Usage: tsbranch [corelist] [off | indirect | all | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  off:      Timestamps are not generated for branch messages"
        echo "  indirect: Generate timestamps for all indirect branch and exception messages"
        echo "  all:      Generate timestamps for all branch, exception, PTCM, and Error messages"
        echo "  help:     Display this message"
        echo ""
        echo "tsbranch with no arguments will display the current setting for tsbranch (off, indirect, all)"
        echo ""
    } elseif {($opt == "off") || ($opt == "indirect") || ($opt == "indirect+exception") || ($opt == "all")} {
        foreach core $coreList {
            setTsBranch $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tsbranch [corelist] [off | indirect | all | help]}
    }
}

proc tsitc {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsitc [corelist] [on | off | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsITC $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "tsitc: set or display if timestamp messages are generated for itc messages"
        echo {Usage: tsitc [corelist] [on | off | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  on:       Timestamp are generated for itc messages"
        echo "  off:      Timestamp are not generated for itc messages"
        echo "  help:     Display this message"
        echo ""
        echo "tsitc with no arguments will display whether or not timestamps are generated for itc messages (on or off)"
        echo ""
    } elseif {($opt == "on") || ($opt == "off")} {
        foreach core $coreList {
            setTsITC $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tsitc [corelist] [on | off | help]}
    }
}

proc tsowner {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tsowner [corelist] [on | off | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTsOwner $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "tsowner: set or display if timestamp messages are generated for ownership messages"
        echo {Usage: tsowner [on | off | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  on:   Timestamp are generated for ownership messages"
        echo "  off:  Timestamp are not generated for ownership messages"
        echo "  help: Display this message"
        echo ""
        echo "tsowner with no arguments will display whether or not timestamps are generated"
        echo "for ownership messages (on or off)"
        echo ""
    } elseif {($opt == "on") || ($opt == "off")} {
        foreach core $coreList {
            setTsOwner $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tsowner [corelist] [on | off | help]}
    }
}

proc stoponwrap {{cores "all"} {opt ""}} {
    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreFunnelList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: stoponwrap [corelist] [on | off | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getTeStopOnWrap $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "stoponwrap: set or display trace buffer wrap mode"
        echo {Usage: stoponwrap [corelist] [on | off | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  on:       Enable stop trace collection when buffer is full (default)"
        echo "  off:      Continue tracing when the buffer fills, causing it to wrap"
        echo "  help:     Display this message"
        echo ""
        echo "stoponwrap with no arguments will display the current status of trace buffer"
        echo "wrap (on or off)"
        echo ""
    } elseif {($opt == "on") || ($opt == "off")} {
        foreach core $coreList {
            setTeStopOnWrap $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: stoponwrap [corelist] [on | off | help]}
    }
}

proc eventmode {{cores "all"} args} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        set args [concat $cores $args]
	set cores "all"
	set coreList [parseCoreList $cores]

	if {$coreList == "error"} {
            echo {Error: Usage: eventmode [corelist] [none | all | sample | trigger | watchpoint | call | interrupt | exception | context | help]}
            return ""
	}
    }

    if {$args == ""} {
        # display current status of evControl
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getEventControl $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tsd
        }

        return $rv
    }

    if {$args == "help"} {
        echo "eventmode: set or display event enabled mode"
        echo {Usage: eventmode [corelist] [none | all | sample | trigger | watchpoint | call | interrupt | exception | context]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  sample:     Enable pc sampling"
        echo "  trigger:    Enable trigger event messagaging"
        echo "  watchpoint: Enable watchpoint event messaging"
        echo "  call:       Enable call event messaging"
        echo "  interrupt:  Enable interrupt event messaging"
        echo "  exception:  Enable exception event messaging"
        echo "  context:    Enable context switch event messaging"
        echo "  all:        Enable all event messaging"
        echo "  none:       Do not generate any event messaging"
        echo "  help:     Display this message"
        echo ""
        echo "eventmode with no arguments will display the current setting for event messaging"
        echo "To enable multiple events, separate the event types with a space"
        echo ""
    } else {
        foreach core $coreList {
            set rc [setEventControl $core $args]
            if {$rc != 0} {
                echo {Error: Usage: eventmode [corelist] [none | all | sample | trigger | watchpoint | call | interrupt | exception | context | help]}
                return ""
            }
        }
    }
    echo -n ""
}


proc tracemode {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
	    set cores "all"

	    set coreList [parseCoreList $cores]
        }

	if {$coreList == "error"} {
            echo {Error: Usage: tracemode [corelist] [all | btm | htm | htmc | none | sample | event | help]}
	    return "error"
        }
    }

    if {$opt == ""} {
        set rv ""

	foreach core $coreList {
            set tsd "core $core: "

	    lappend tsd [getTargetTraceMode $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tsd
        }

        return $rv
    }

    if {$opt == "help"} {
        echo "tracemode: set or display trace type (sync, sync+btm)"
        echo {Usage: tracemode [corelist] [none | all | btm | htm | htmc | sample | event | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  btm:      Generate both sync and btm trace messages"
        echo "  htm:      Generate sync and htm trace messages (with return stack optimization or repeat branch optimization)"
        echo "  htmc      Generate sync and conservitive htm trace messages (without return stack optimization or repeat branch optimization)"
        echo "  sample    Generate PC sample trace using In Circuit Trace mode"
        echo "  event     Generate event trace. Use eventmode to select events"
        echo "  all:      Generate both sync and btm or htm trace messages (whichever is supported by hardware)"
        echo "  none:     Do not generate sync or btm trace messages"
        echo "  help:     Display this message"
        echo ""
        echo "tracemode with no arguments will display the current setting for the type"
        echo "of messages to generate (none, sync, or all)"
        echo ""
    } elseif {($opt == "sample") || ($opt == "event") || ($opt == "all") || ($opt == "none") || ($opt == "btm") || ($opt == "htm") || ($opt == "htmc") || ($opt == "btm+sync") || ($opt == "htm+sync") || ($opt == "htmc+sync")} {
        foreach core $coreList {
            setTargetTraceMode $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tracemode [corelist] [all | btm | htm | htmc | none | sample | event | help]}
    }
}

proc itc {{cores "all"} {opt ""} {mask ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$mask == ""} {
            set mask $opt
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: itc [corelist] [off | ownership | all | all+ownership | mask [ nn ] | trigmask [ nn ] | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [getITC $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "itc: set or display itc settings"
        echo {Usage: itc [corelist] [off | ownership | all | all+ownership | mask [ nn ] | trigmask [ nn ] | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  off:           Disable ITC message generation"
        echo "  ownership:     Enable ITC messages for stiumlus 15 and 31 only. Set the stimulus"
        echo "                 mask to 0x80008000"
        echo "  all:           Enable ITC messages for all stimulus 0 - 31. Set the stimulus"
        echo "                 mask to 0xffffffff"
        echo "  all+ownership: Generate ownership messages for stimulus 15 and 32, and"
        echo "                 ITC messages for all other stimulus. Set the stimulus mask to"
        echo "                 0xffffffff"
        echo "  mask nn:       Set the stimulus mask to nn, where nn is a 32 bit number. Note"
        echo "                 nn should be prefixed with 0x if it is a hex number, or just 0 if"
        echo "                 it is an octal number; otherwise it will be interpreted as a decimal"
        echo "                 number. Does not effect the ITC mode (off, ownership, all, all+ownership)."
        echo "                 itc mask without nn displays the current value of the mask"
        echo "  trigmask nn:   Set the trigger enable mask to nn, where nn is a 32 bit number. Note"
        echo "                 nn should be prefixed with 0x if it is a hex number, or just 0 if"
        echo "                 it is an octal number; othwise it will be interpreted as a decimal"
        echo "                 number. Does not effect the ITC mode (off, ownership, all, all+ownership)."
        echo "                 itc trigmask without nn displays the current value of the trigger enable mask"
        echo "  help:          Display this message"
        echo ""
        echo "itc with no arguments will display the current itc settings"
        echo ""
    } elseif {$opt == "mask"} {
        if {$mask == "" } {
            foreach core $coreList {
                set rv ""

                foreach core $coreList {
                    set tsd "core $core: "

                    lappend tsd [getITCMask $core]

                    if {$rv != ""} {
                    append rv "; "
                    }

                    append rv $tsd
                }

                return $rv
            }
        } else {
            foreach core $coreList {
                setITCMask $core $mask
            }
        }
    } elseif {$opt == "trigmask"} {
        if {$mask == ""} {
            foreach core $coreList {
                set rv ""

                foreach core $coreList {
                    set tsd "core $core: "

                    lappend tsd [getITCTriggerMask $core]

                    if {$rv != ""} {
                    append rv "; "
                    }

                    append rv $tsd
                }

                return $rv
            }
        } else {
            foreach core $coreList {
                setITCTriggerMask $core $mask
            }
        }
    } elseif {($opt == "off") || ($opt == "all") || ($opt == "ownership") || ($opt == "all+ownership")} {
        foreach core $coreList {
            setITC $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: itc [corelist] [off | ownership | all | all+ownership | mask [ nn ] | trigmask [ nn ] | help]}
    }
}

proc maxicnt {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: maxicnt [corelist] [5 - 10 | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [expr [getMaxIcnt $core] + 5]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "maxicnt: set or dipspaly the maximum i-cnt field"
        echo {Usage: maxicnt [corelist] [nn | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  nn:       Set max i-cnt value to 2^(nn+5). nn must be between 5 and 10 for"
        echo "            a range between 32 and 1024"
        echo "  help:     Display this message"
        echo ""
        echo "maxicnt with no arguments will display the current maximum i-cnt value"
        echo ""
    } elseif {$opt >= 5 && $opt <= 10} {
        foreach core $coreList {
            setMaxIcnt $core [expr $opt - 5]
        }
        echo -n ""
    } else {
        echo {Error: Usage: maxicnt [corelist] [5 - 10 | help]}
    }
}

proc maxbtm {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: maxbtm [corelist] [5 - 16 | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        set rv ""

        foreach core $coreList {
            set tsd "core $core: "

            lappend tsd [expr [getMaxBTM $core] + 5]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $tsd
        }
        return $rv
    }

    if {$opt == "help"} {
        echo "maxbtm: set or display the maximum number of BTMs between Sync messages"
        echo {Usage: maxbtm [corelist] [nn | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  nn:       Set the maximum number of BTMs between Syncs to nn. nn must be between"
        echo "            5 and 16 for a range between 32 and 65536"
        echo "  help:     Display this message"
        echo ""
        echo "maxbtm with no arguments will display the current maximum number of BTMs"
        echo "between sync messages"
        echo ""
    } elseif {$opt >= 5 && $opt <= 16} {
        foreach core $coreList {
            setMaxBTM $core [expr $opt - 5]
        }
        echo -n ""
    } else {
        echo {Error: Usage: maxbtm [corelist] [5 - 16 | help]}
    }
}

proc setreadptr {core ptr} {
    global traceBaseAddrArray
    global te_sinkrp_offset

#    echo "setreadptr($core [format 0x%08lx $ptr])"

    mww [expr $traceBaseAddrArray($core) + $te_sinkrp_offset] $ptr
}

proc setcareadptr {core ptr} {
    global CABaseAddrArray
    global ca_sink_rp_offset

#    echo "setcareadptr($core [format 0x%08lx $ptr])"

    mww [expr $CABaseAddrArray($core) + $ca_sink_rp_offset] $ptr
}

proc readSRAMData {core} {
    global traceBaseAddrArray
    global te_sinkdata_offset

    return [word [expr $traceBaseAddrArray($core) + $te_sinkdata_offset]]
}

proc readCASRAMData {core} {
    global CABaseAddrArray
    global ca_sink_data_offset

#    echo "redCASRAMData($core)"

    return [word [expr $CABaseAddrArray($core) + $ca_sink_data_offset]]
}

proc computeStartEnd {buffStart writePtr buffSize wrap sow numWant start1 end1 start2 end2} {
# if sow == on, want oldest (first - forwards from beginning (0) )
# if sow == off, want newest (last - backwards from end (writePtr))
    global verbose

    if {$verbose > 1} {
        echo "cse: start=[format 0x%016lx $buffStart] wp=[format 0x%08lx $writePtr] bs=[format 0x%08lx $buffSize] wrap=$wrap sow=$sow limit=[format 0x%08lx $numWant]"
    }

    upvar $start1 s1
    upvar $end1 e1
    upvar $start2 s2
    upvar $end2 e2

    set buffEnd [expr $buffStart + $buffSize]

    if {$wrap == 0 } {
        # buffer has not wrapped
        set s2 0;
        set e2 0;

        if {$numWant == 0} {
            # want whole buffer buffStart to writePtr

            set s1 $buffStart
            set e1 $writePtr

            if {$verbose > 1} {
                echo "cse-1: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1]  s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
            }

        } else {
            # numWant != 0

            if {$numWant >= [expr $buffStart - $writePtr]} {
                # don't have numWant bytes in buffer. Set end to number we have want buffStart to writePtr

                set s1 $buffStart
                set e1 $writePtr

                if {$verbose > 1} {
                    echo "cse-2: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1]  s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                }

            } elseif {$sow == "on"} {
                # want beginning of buffer (oldest) buffStart to buffStart + numwant

                set s1 $buffStart
                set e1 [expr $buffStart + $numWant]

                if {$verbose > 1} {
                    echo "cse-3: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1]  s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                }

            } else {
                # want end of buffer (newest) want writPtr - numWant to writePtr

                set s1 [expr $writePtr - $numWant]
                set e1 $writePtr

                if {$verbose > 1} {
                    echo "cse-4: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1]  s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                }
            }
        }
    } else {
        # buffer has wrapped
        if {($numWant == 0) || ($numWant >= $buffSize)} {
            # want it all want writePtr to buffEnd, then buffStart to writePtr

            set s1 $writePtr
            set e1 $buffEnd
            set s2 $buffStart
            set e2 $writePtr
            if {$verbose > 1} {
                echo "cse-5: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
            }
        } else {
            if {$sow == "on"} {
                # want beginning of buffer (oldest)

                if {$numWant >= $buffSize} {
                    # want whole buffer
                    # want writePtr to buffEnd, then buffStart to writePtr

                    set s1 $writePtr
                    set e1 $buffEnd
                    set s2 $buffStart
                    set e2 $writePtr
                    if {$verbose > 1} {
                        echo "cse-6: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                    }
                } else {
                    # numWant < buffSize

                    if {($writePtr + $numWant) > $buffEnd} {
                    # want writePtr to buffEnd, buffSart to (buffStart + (buffEnd - writePtr)
                        set s1 $writePtr
                        set e1 $buffEnd
                        set s2 $buffStart
                        set remaining [expr $numWant - ($buffEnd - $writePtr)]
                        set e2 [expr $buffStart + $remaining]
                        if {$verbose > 1} {
                            echo "cse-7: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                        }
                    } else {
                        # want writePtr to writePtr + numWant
                        set s1 $writePtr
                        set e1 [expr $writePtr + $numWant]
                        set s2 0
                        set e2 0
                        if {$verbose > 1} {
                            echo "cse-8: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                        }
                    }
                }
            } else {
                # want end of buffer (newest)
                # remember, buffer has wrapped, numWant < buffSize
                if {($buffStart + $numWant) > $writePtr} {
                    # want buffEnd - (numWant - (writePtr - BuffSart) to buffEnd, buffStart to writePtr

                    set s1 [expr $buffEnd - ($numWant - ($writePtr - $buffStart))]
                    set e1 $buffEnd

                    set s2 $buffStart
                    set e2 $writePtr
                    if {$verbose > 1} {
                        echo "cse-9: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                    }
                } else {
                    # want writePtr - numWant to writePtr

                    set s1 [expr $writePtr - $numWant]
                    set e1 $writePtr
                    set e2 0
                    set s2 0
                    if {$verbose > 1} {
                        echo "cse-10: s1=[format 0x%016lx $s1] e1=[format 0x%016lx $e1] s2=[format 0x%016lx $s2] e2=[format 0x%016lx $e2]"
                    }
                }
            }
        }
    }
}

proc writeSRAM {core file limit} {
    global verbose
    
    if { $verbose > 1 } {
        echo ""
    }

    set tracewp [gettracewp $core]
    if {($tracewp & 1) == 0} {
        set wrap 0
    } else {
        set wrap 1
        set tracewp [expr $tracewp & 0xfffffffe]
    }

    set buffersize [getTraceBufferSize $core]

    set tracewp [expr $tracewp & 0xfffffffe]

    set stop_on_wrap [getTeStopOnWrap $core]

    computeStartEnd 0 $tracewp $buffersize $wrap $stop_on_wrap $limit start1 end1 start2 end2

    if {$verbose > 1} {
    echo "start1: [format 0x%08lx $start1] end1: [format 0x%08lx $end1] start2: [format 0x%08lx $start2] end2: [format 0x%08lx $end2]"
    }

    if {$file == "stdout"} {
    # write start1 to end1, start2 to end2

        # this version reads a word at a time from the trace buffer

           if {$verbose > 1} {
            if {$star1 != $end1} {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }
        }

        setreadptr $core $start1
        set f ""

        for {set i $start1} {$i < $end1} {incr i 4} {
            set w [format %08x [eval readSRAMData $core]]
            append f $w
        }

           if {$verbose > 1} {
            if {$star2 != $end2} {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }
        }

        setreadptr $core $start2

        for {set i $start2} {$i < $end2} {incr i 4} {
            set w [format %08x [eval readSRAMData $core]]
            append f $w
        }

        return $f
    } else {
        set fp [open "$file" wb]

        if {$start1 != $end1} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }

            writeSRAMdata $core $start1 $end1 $fp
        }

        if {$start2 != $end2} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }

            writeSRAMdata $core $start2 $end2 $fp
        }

        close $fp

    return ""
    }
}

proc writeCASRAM {core file limit} {
    global verbose
    
    if { $verbose > 1 } {
        echo ""
    }

    set tracewp [getcatracewp $core]
    if {($tracewp & 1) == 0} {
        set wrap 0
    } else {
        set wrap 1
        set tracewp [expr $tracewp & 0xfffffffe]
    }

    set buffersize [getCATraceBufferSize $core]

    set tracewp [expr $tracewp & 0xfffffffe]

    set stop_on_wrap [getCAStopOnWrap $core]

    computeStartEnd 0 $tracewp $buffersize $wrap $stop_on_wrap $limit start1 end1 start2 end2

    if {$file == "stdout"} {
    # write start1 to end1, start2 to end2

        # this version reads a word at a time from the trace buffer

           if {$verbose > 1} {
            if {$star1 != $end1} {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }
        }

        setcareadptr $core $start1
        set f ""

        for {set i $start1} {$i < $end1} {incr i 4} {
            set w [format %08x [eval readCASRAMData $core]]
            append f $w
        }

           if {$verbose > 1} {
            if {$star2 != $end2} {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }
        }

        setreadptr $core $start2

        for {set i $start2} {$i < $end2} {incr i 4} {
            set w [format %08x [eval readCASRAMData $core]]
            append f $w
        }

        return $f
    } else {
        set fp [open "$file" wb]

        if {$start1 != $end1} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }

            writeCASRAMdata $core $start1 $end1 $fp
        }

        if {$start2 != $end2} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }

            writeCASRAMdata $core $start2 $end2 $fp
        }

        close $fp

    return ""
    }
}

proc _writeSRAMdata { core tracebegin traceend fp } {
    for {set i $tracebegin} {$i < $traceend} {incr i 4} {
        pack w [eval readSRAMData $core] -intle 32
        puts -nonewline $fp $w
    }
}

proc writeSRAMdata { core tracebegin traceend fp } {
    global traceBaseAddrArray
    global te_sinkdata_offset

    set length [expr ($traceend - $tracebegin) / 4]

    if {$length > 0} {
        setreadptr $core $tracebegin

        set daddr  [expr $traceBaseAddrArray($core) + $te_sinkdata_offset]
        set data [riscv repeat_read $length $daddr 4] 
        set packed ""

        foreach value [split $data "\r\n "] {
            if {$value != ""} {
                incr j
                pack w 0x$value -intle 32
                append packed $w
            }
        }

        puts -nonewline $fp $packed
    }
}

proc writeCASRAMdata { core tracebegin traceend fp } {
    global CABaseAddrArray
    global ca_sink_data_offset

    set length [expr ($traceend - $tracebegin) / 4]
    if {$length>0} {
        setcareadptr $core $tracebegin

        set daddr  [expr $CABaseAddrArray($core) + $ca_sink_data_offset]
        set data [riscv repeat_read $length $daddr 4] 
        set packed ""

        foreach value [split $data "\r\n "] {
            if {$value != ""} {
                pack w 0x$value -intle 32
                append packed $w
            }
        }
        puts -nonewline $fp $packed
    }
}

proc getCapturedTraceSize { core } {
    global num_funnels
    global num_cores
    global verbose

#    echo "getCapturedTraceSize($core)"

    if {$num_funnels > 0} {
        set s [getSink funnel]
        switch [string toupper $s] {
            "SRAM" { return [getTraceBufferSizeSRAM funnel]}
            "SBA"  { return [getTraceBufferSizeSBA funnel]}
        }
    } else {
        set s [getSink $core]
        switch [string toupper $s] {
            "SRAM" { return [getTraceBufferSizeSRAM $core]}
            "SBA"  { return [getTraceBufferSizeSBA $core]}
        }
    }
}

proc getTraceBufferSizeSRAM {core} {
    set tracewp [gettracewp $core]

#	echo "getTraceBufferSizeSRAM($core)"

    if {($tracewp & 1) == 0 } { ;
        # buffer has not wrapped
        set tracebegin 0
        set traceend $tracewp
        return [expr $traceend - $tracebegin]
    }

    return [getTraceBufferSize $core]
}

proc getTraceBufferSizeSBA {core} {
    global traceBaseAddrArray
    global te_sinkbase_offset
    global te_sinklimit_offset

#    echo "getTraceBufferSizeSBA($core)"

    set tracewp [gettracewp $core]
    if { ($tracewp & 1) == 0 } { 
        # buffer has not wrapped
        set tracebegin [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
        set traceend $tracewp
        return [expr $traceend - $tracebegin]
    } 
    return [getTraceBufferSize $core]
}

proc writeSBA {core file limit} {
    global verbose
    
    if { $verbose > 1 } {
        echo ""
        echo "limit: $limit"
    }

    set baseAddr [getSBABaseAddress $core]
    set tracewp [gettracewp $core]

    if {($tracewp & 1) == 0} {
        set wrap 0
    } else {
        set wrap 1
        set tracewp [expr $tracewp & 0xfffffffffffffffe]
    }

    set buffersize [getTraceBufferSize $core]

    set stop_on_wrap [getTeStopOnWrap $core]

    computeStartEnd $baseAddr $tracewp $buffersize $wrap $stop_on_wrap $limit start1 end1 start2 end2

    if {$verbose > 1} {
        echo "start1: [format 0x%08lx $start1] end1: [format 0x%08lx $end1] start2: [format 0x%08lx $start2] end2: [format 0x%08lx $end2]"
    }

    if {$file == "stdout"} {
    # write start1 to end1, start2 to end2

        # this version reads a word at a time from the trace buffer

           if {$verbose > 1} {
            if {$star1 != $end1} {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }
        }

        setreadptr $core $start1
        set f ""

        for {set i $start1} {$i < $end1} {incr i 4} {
            set w [format %08x [eval readSBAData $core]]
            append f $w
        }

           if {$verbose > 1} {
            if {$star2 != $end2} {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }
        }

        setreadptr $core $start2

        for {set i $start2} {$i < $end2} {incr i 4} {
            set w [format %08x [eval readSRBAData $core]]
            append f $w
        }

        return $f
    } else {
        set fp [open "$file" wb]

        if {$start1 != $end1} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start1] to [format %08x $end1], [expr $end1 - $start1] bytes"
            }

            writeSBAdataX $start1 $end1 $fp
        }

        if {$start2 != $end2} {
            if { $verbose > 1 } {
                echo "Trace from [format 0x%08x $start2] to [format 0x%08x $end2], [expr $end2 - $start2] bytes"
            }

            writeSBAdataX $start2 $end2 $fp
        }

        close $fp

    return ""
    }
}

# The slow way...not used, but left here for posterity.

proc writeSBAdata { tb te fp } {
    for {set i $tb} {$i < $te} {incr i 4} {
        pack w [word $i] -intle 32
        puts -nonewline $fp $w
    }
}

proc writeSBAdataXcs { tb te cs fp } {
    global verbose

    set length [expr $te - $tb]
    if {$length < $cs} {
        # total length is less than chunk size, return and
        # process next smallest chunk
        return $tb
    }

    set extra [expr [expr $te - $tb] % $cs]
    set extra_start [expr $te - $extra]
    set length [expr $extra_start - $tb]
    set chunks [expr $length / $cs]
    if {$verbose > 1} {
        echo [format "Range : %08X to %08X" $tb $extra_start]
        echo "Chunks: $chunks @ $cs bytes/ea with $extra remaining byte"
    }

    set elems [expr $cs >> 2]
    for {set i $tb} {$i < $extra_start} {incr i $cs} {
        if {$verbose > 2} {
            echo [format "Chunk: %08X to %08X" $i [expr $i +$cs]]
        }


        mem2array x 32 $i $elems
        for {set j 0} {$j < $elems} {incr j 1} {
            pack w $x($j) -intle 32
            puts -nonewline $fp $w
        }
    }

    return $extra_start
}

proc writeSBAdataX { tb te fp } {
    global verbose

    # Set the chunk size, must be power of 2
    set cs 256

    set start_addr $tb

    # See if our buffer is a multiple of 256 bytes, if not
    # figure out how many extra bytes at the end we need to
    # cpature.
    set length [expr $te - $start_addr]
    set extra [expr $length % $cs]

    if {$verbose > 1} {
        echo [format "Capturing from %08X to %08X" $start_addr $te]
    }

    while {$start_addr <= $te && $cs > 0} {
        set start_addr [writeSBAdataXcs $start_addr $te $cs $fp]
        set length [expr $te - $start_addr]
        set cs [expr $cs >> 1]
        set mod4 [expr $length % 4]
        if {$mod4 == 0} {
            set cs $length
            if {$verbose > 1} {
                echo "Set final cs = $cs"
            }
        }
    }

}

proc wtb {{file "trace.rtd"} {limit 0}} {
    global num_funnels
    global num_cores
    global verbose

#    echo "wtb($file $limit)"

    if  { $verbose > 0 } {
        echo -n "collecting trace..."
    }

    riscv set_prefer_sba off

    if {$num_funnels > 0} {
        set s [getSink funnel]

        switch [string toupper $s] {
        "SRAM" { set f [writeSRAM funnel $file $limit]}
        "SBA"  { set f [writeSBA funnel $file $limit]}
        }
    } else {
        set coreList [parseCoreList "all"]

        foreach core $coreList {
            set s [getSink $core]

            if {$num_cores > 1} {
                set fn $file.$core
            } else {
                set fn $file
            }

            switch [string toupper $s] {
            "SRAM" { set f [writeSRAM $core $fn $limit]}
            "SBA"  { set f [writeSBA $core $fn $limit]}
            }

        }
    }

    riscv set_prefer_sba on

    if { $verbose > 0 } {
        echo "done."
    }

    if {$file == "stdout"} {
        return $f
    }
}

proc wcab {{file "trace.cat"} {limit 0}} {
    global num_cores
    global verbose

#    echo "wcab($file $limit)"

    if  { $verbose > 0 } {
        echo -n "collecting CA trace..."
    }

    riscv set_prefer_sba off

    set coreList [parseCoreList "all"]

    foreach core $coreList {
        set s [getCASink $core]

        if {$num_cores > 1} {
            set fn $file.$core
        } else {
            set fn $file
        }

        switch [string toupper $s] {
        "SRAM" { set f [writeCASRAM $core $fn $limit]}
        default { echo "wcatb: unsupported CA sink: $s"
                  set f ""
                }
        }
    }

    riscv set_prefer_sba on

    if { $verbose > 0 } {
        echo "done."
    }

    if {$file == "stdout"} {
        return $f
    }
}

proc clearTraceBuffer {core} {
    global traceBaseAddrArray
    global te_sinkrp_offset
    global te_sinkwp_offset
    global te_sinkbase_offset

#    echo "clearTraceBuffer($core)"

    set s [getSink $core]
    switch [string toupper $s] {
        "SRAM" { 
            mww [expr $traceBaseAddrArray($core) + $te_sinkrp_offset] 0
            mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] 0
        }
        "SBA" { 
            set t [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
            mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] $t
            mww [expr $traceBaseAddrArray($core) + $te_sinkrp_offset] $t
        }
    }
}

proc cleartrace {{cores "all"}} {
    set coreList [parseCoreFunnelList $cores]

#    echo "cleartrace($cores)"

    if {$coreList == "error"} {
        echo {Error: Usage: cleartrace [corelist]}
        return "error"
    }

    foreach core $coreList {
        clearTraceBuffer $core
    }
}

proc readts {} {
    global traceBaseAddrArray
    global ts_control_offset

#    echo "readts()"

    echo "[wordhex [expr $traceBaseAddrArray(0) + $ts_control_offset]]"
}

proc gettracewp {core} {
    global te_sinkwp_offset
    global traceBaseAddrArray
    global te_sinkbasehigh_offset
    global verbose

#    echo "gettracewp($core)"

    set tracewp [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]

    set tracebasehi [word [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]

    set baseAddr [expr $tracewp + ($tracebasehi << 32)]

    if {$verbose > 1} {
        echo "tracewp: [format 0x%016lx $baseAddr]"
    }

    return $baseAddr
}

proc getCASink {core} {
    global CABaseAddrArray
    global ca_control_offset

#    echo "getCASink($core)"

    set t [word [expr $CABaseAddrArray($core) + $ca_control_offset]]
    set t [expr ($t >> 28) & 0x0f]

    switch $t {
        4 { return "SRAM"   }
        default { return "Reserved" }
    }
}

proc getcatracewp {core} {
    global ca_sink_wp_offset
    global CABaseAddrArray

#    echo "getcatracewp($core)"

    set tracewp [word [expr $CABaseAddrArray($core) + $ca_sink_wp_offset]]

    return $tracewp
}

proc getSink {core} {
    global traceBaseAddrArray
    global te_control_offset
    global te_impl_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 28) & 0x0f]

#    echo "getSink($core)"

    switch $t {
        0 { 
            set t [word [expr $traceBaseAddrArray($core) + $te_impl_offset]]
            set t [expr ($t >> 4) & 0x1f]
            switch $t {
                1    { return "SRAM" }
                2    { return "ATB"  }
                4    { return "PIB"  }
                8    { return "SBA"  }
                16   { return "FUNNEL" }
                default { return "Reserved" }
            }
        }
        4 { return "SRAM"   }
        5 { return "ATB"    }
        6 { return "PIB"    }
        7 { return "SBA"    }
        8 { return "FUNNEL" }
        default { return "Reserved" }
    }
}

proc getTraceBufferSize {core} {
    global traceBaseAddrArray
    global te_sinkwp_offset
    global te_sinkbase_offset
    global te_sinklimit_offset
    global trace_buffer_width

#    echo "getTraceBufferSize($core)"

    switch [string toupper [getSink $core]] {
        "SRAM" { 
            set t [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]
            mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] 0xfffffffc
            set size [expr [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]] + $trace_buffer_width]
            mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] $t
            return $size
            }
        "SBA"  { 
            set start [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
            set end [word [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]
            set size [expr $end - $start + $trace_buffer_width]
            return $size
            }
    }

    return 0
}

proc getCATraceBufferSize {core} {
    global CABaseAddrArray
    global ca_sink_wp_offset
    global ca_sinkbase_offset
    global ca_sinklimit_offset

#    echo "getCATraceBufferSize($core)"

    switch [string toupper [getCASink $core]] {
    "SRAM" { set t [word [expr $CABaseAddrArray($core) + $ca_sink_wp_offset]]
             mww [expr $CABaseAddrArray($core) + $ca_sink_wp_offset] 0xfffffffc
             set size [expr [word [expr $CABaseAddrArray($core) + $ca_sink_wp_offset]] + 4]
             mww [expr $CABaseAddrArray($core) + $ca_sink_wp_offset] $t
             return $size
           }
    default { echo "unsupported CA trace buffer sink" }
    }

    return 0
}

proc setSink {core type {base ""} {size ""}} {
    global traceBaseAddrArray
    global te_control_offset
    global te_impl_offset
    global te_sinkbase_offset
    global te_sinkbasehigh_offset
    global te_sinklimit_offset
    global trace_buffer_width
    global te_sinkwp_offset

#    echo "setSink($core $type base: $base size: $size)"

    switch [string toupper $type] {
        "SRAM"   { set b 4 }
        "ATB"    { set b 5 }
        "PIB"    { set b 6 }
        "SBA"    { set b 7 }
        "FUNNEL" { set b 8 }
        default  { return "Error: setSink(): Invalid sync" }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_impl_offset]]

    set t [expr $t & (1 << $b)]

    if {$t == 0} {
        return "Error: setSink(): sink type $type not supported on core $core"
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    set t [expr $t & ~(0xf << 28)]
    set t [expr $t | ($b << 28)]

    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t

    if {[string compare -nocase $type "sba"] == 0} {
        if {$base != ""} {
            set limit [expr $base + $size - $trace_buffer_width];

            if {($limit >> 32) != ($base >> 32)} {
                return "Error: setSink(): buffer can't span a 2^32 address boundry"
            } else {
                mww [expr $traceBaseAddrArray($core) + $te_sinkbase_offset] [expr $base & 0xffffffff]
                set b [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
                if {$b != [expr $base & 0xffffffff]} {
                    return "Error: setSink(): invalid buffer address for SBA"
                }
                mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] [expr $base & 0xffffffff]

                mww [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset] [expr $base >> 32]
                set b [word [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]
                if {$b != [expr $base >> 32]} {
                    return "Error: setSink(): invalid buffer address for SBA"
                }

                mww [expr $traceBaseAddrArray($core) + $te_sinklimit_offset] [expr $limit & 0xffffffff]
                set b [word [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]
                if {$b != [expr $limit & 0xffffffff]} {
                    return "Error: setSink(): invalid buffer size for SBA"
                }
            }
        }
    }

    return ""
}

proc sinkstatus {{cores "all"} {action ""}} {
    set coreList [parseCoreFunnelList $cores]

#    echo "sinkstatus($cores $action)";

    if {$coreList == "error"} {
        if {$action == ""} {
            set action $cores
            set cores "all"

            set coreList [parseCoreFunnelList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: sinkstatus [corelist] [reset | help]}
            return "error"
        }
    }

    if {$action == ""} {
        # display current status of teSinkError
        set rv ""

        foreach core $coreList {
            if {$core == "funnel"} {
                set te "$core: "
            } else {
                set te "core $core: "
            }

            lappend te [getSinkError $core]

            if {$rv != ""} {
                append rv "; "
            }

            append rv $te
        }

        return $rv
    }

    if {$action == "help"} {
        echo "sinkstatus: display or clear status of teSinkError bit in the teControl register"
        echo {Usage: sinkstatus [corelist] [clear | help]}
        echo "  clear:    Clear the teSinkError bit"
        echo "  help:     Display this message"
        echo ""
        echo "sinkstatus with no arguments will display the status of the teSinkError bit for"
        echo "all cores and the funnel (if present)"
        echo ""
    } elseif {$action == "clear"} {
        foreach core $coreList {
            clearSinkError $core
        }
    }
}

proc tracedst {{cores ""} {dst ""} {addr ""} {size ""}} {
    global traceBaseAddrArray
    global te_control_offset
    global te_impl_offset
    global te_sinkbase_offset
    global te_sinkbasehigh_offset
    global te_sinklimit_offset
    global num_funnels
    global te_sinkwp_offset
    global te_sinkrp_offset
    global trace_buffer_width

    switch [string toupper $cores] {
        ""       { 
            set cores "all" }
        "SBA"    { 
            set size $addr
            set addr $dst
            set dst "sba"
            set cores "all" 
        }
        "SRAM"   { 
            set dst "sram"
            set cores "all" 
        }
        "ATB"    { 
            set dst "atb"
            set cores "all" 
        }
        "PIB"    { 
            set dst "pib"
            set cores "all" 
        }
        "HELP"   { 
            set dst "help"
            set cores "all" 
        }
        "FUNNEL" { 
            switch [string toupper $dst] {
                ""     {}
                "SBA"  {}
                "SRAM" {}
                "ATB"  {}
                "PIB"  {}
                "HELP" {}
                "FUNNEL" {}
                default { set size $addr
                    set addr $dst
                    set dst $cores
                    set cores "all"
                }
            }
        }
        default  {}
    }

    set coreFunnelList [parseCoreFunnelList $cores]
    set coreList [parseCoreList $cores]

    if {$dst == ""} {
        set teSink {}
        foreach core $coreFunnelList {
            set sink [getSink $core]

            if {$teSink != ""} {
                append teSink "; "
            }

            append teSink " core: $core $sink"

            switch [string toupper $sink] {
                "SRAM"  { 
                    # get size of SRAM
                    set t [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]
                    mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] 0xfffffffc
                    set size [expr [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]] + $trace_buffer_width]
                    mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] $t
                    set teSink "$teSink, size: $size bytes"
                }
                "SBA"   { 
                    set sinkBase [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
                    set sinkLimit [word [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]
                    set sinkBaseHigh [word [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]
                    set sinkBase [expr ($sinkBaseHigh << 32) + $sinkBase]
                    set sinkLimit [expr ($sinkBaseHigh << 32) + $sinkLimit]
                    set teSink "$teSink, base: [format 0x%08x $sinkBase], size: [expr $sinkLimit - $sinkBase + $trace_buffer_width] bytes"
                }
            }
        }

        return $teSink
    } elseif {[string compare -nocase $dst "help"] == 0} {
        echo "tracedst: set or display trace sink for cores and funnel"
        echo {Usage: tracedst [corelist] [sram | atb | pib | funnel | sba [base size] | help]}
        echo "  corelist: Comma separated list of core numbers, funnel, or 'all'. Not specifying is equivalent to all"
        echo "  sram:     Set the trace sink to on-chip sram"
        echo "  atb:      Set the trace sink to the ATB"
        echo "  pib:      Set the trace sink to the PIB"
        echo "  funnel:   set the trtace sink to the funnel"
        echo "  sba:      Set the trace sink to the system memory at the specified base and limit. If no specified"
        echo "            they are left as previously programmed"
        echo "  base:     The address to begin the sba trace buffer in system memory at"
        echo "  size:     Size of the sba buffer in bytes. Must be a multiple of 4"
        echo "  help:     Display this message"
        echo ""
        echo "tracedst with no arguments will display the trace sink for all cores and the funnel (if present)"
        echo ""
        echo "If no cores are specified and there is no trace funnel, all cores will be programed with the"
        echo "sink specified. If no cores are specified and there is a trace funnel, all cores will be"
        echo "programmed to sink to the funnel and the funnel will be programmed to use the sink specified"
        echo ""
    } elseif {[string compare -nocase $dst "atb"] == 0} {
        if {$cores == "all"} {
            if {$num_funnels > 0} {
            foreach core $coreList {
                set rc [setSink $core funnel]
                if {$rc != ""} {
                return $rc
                }
            }

            set rc [setSink funnel "atb"]
            if {$rc != ""} {
                return $rc
            }
            } else {
            foreach core $coreList {
                set rc [setSink $core "atb"]
                if {$rc != ""} {
                return $rc
                }
            }
            }
        } else {
            foreach core $coreFunnelList {
            set rc [setSink $core "atb"]
            if {$rc != ""} {
                return $rc
            }
            }
        }
    } elseif {[string compare -nocase $dst "pib"] == 0} {
        if {$cores == "all"} {
            if {$num_funnels > 0} {
                foreach core $coreList {
                    set rc [setSink $core funnel]
                    if {$rc != ""} {
                    return $rc
                    }
                }

                set rc [setSink funnel "pib"]
                if {$rc != ""} {
                    return $rc
                }
            } else {
                foreach core $coreList {
                    set rc [setSink $core "pib"]
                    if {$rc != ""} {
                    return $rc
                    }
                }
            }
        } else {
            foreach core $coreFunnelList {
                set rc [setSink $core "pib"]
                if {$rc != ""} {
                    return $rc
                }
            }
        }
    } elseif {[string compare -nocase $dst "sram"] == 0} {
        if {$cores == "all"} {
            if {$num_funnels > 0} {
                foreach core $coreList {
                    set rc [setSink $core funnel]
                    if {$rc != ""} {
                        return $rc
                    }
                    cleartrace $core
                }

                set rc [setSink funnel "sram"]
                if {$rc != ""} {
                    return $rc
                }
                cleartrace funnel
            } else {
                foreach core $coreList {
                    set rc [setSink $core "sram"]
                    if {$rc != ""} {
                    return $rc
                    }
                    cleartrace $core
                }
            }
        } else {
            foreach core $coreFunnelList {
                set rc [setSink $core "sram"]
                if {$rc != ""} {
                    return $rc
                }
                cleartrace $core
            }
        }
    } elseif {[string compare -nocase $dst "sba"] == 0} {
    # set sink to system ram at address and size specified (if specified)

    if {$cores == "all"} {
        if {$num_funnels > 0} {
            foreach core $coreList {
                set rc [setSink $core funnel]
                if {$rc != ""} {
                return $rc
                }
                cleartrace $core
            }

            set rc [setSink funnel "sba" $addr $size]
            if {$rc != ""} {
                return $rc
            }
            cleartrace funnel
            } else {
                foreach core $coreList {
                    set rc [setSink $core "sba" $addr $size]
                    if {$rc != ""} {
                    return $rc
                    }
                    cleartrace $core
                }
            }
        } else {
            foreach core $coreFunnelList {
                set rc [setSink $core "sba" $addr $size]
                if {$rc != ""} {
                    return $rc
                }
                cleartrace $core
            }
        }
    } elseif {[string compare -nocase $dst funnel] == 0} {
        if {$num_funnels == 0} {
            return "Error: funnel not present"
        }

        foreach $core $coreList {
            set rc [setSink $core funnel]
            if {$rc != ""} {
            echo $rc
            return $rc
            }
            cleartrace $core
        }
    } else {
        echo {Error: Usage: tracedst [sram | atb | pib | sba [base size] | help]}
    }

    return ""
}

proc trace {{cores "all"} {opt ""}} {
    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreFunnelList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: trace [corelist] [on | off | reset | settings | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
        set rv ""

        foreach core $coreList {
            if {$core == "funnel"} {
            set te "$core: "
            } else {
            set te "core $core: "
            }

            lappend te [getTraceEnable $core]

            if {$rv != ""} {
            append rv "; "
            }

            append rv $te
        }

        return $rv
    }

    if {$opt == "help"} {
        echo "trace: set or display the maximum number of BTMs between Sync messages"
        echo {Usage: trace [corelist] [on | off | reset | settings | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  on:       Enable tracing"
        echo "  off:      Disable tracing"
        echo "  reset:    Reset trace encoder"
        echo "  settings: Display current trace related settings"
        echo "  help:     Display this message"
        echo ""
        echo "trace with no arguments will display if tracing is currently enabled for all cores (on or off)"
        echo ""
    } elseif {$opt == "on"} {
        foreach core $coreList {
            enableTraceEncoderManual $core
            enableTracingManual $core
        }
    } elseif {$opt == "off"} {
        foreach core $coreList {
            disableTraceEncoderManual $core
        }
    } elseif {$opt == "reset"} {
        foreach core $coreList {
            resetTrace $core
        }
    } elseif {$opt == "settings"} {
        # build a cores option without funnel

        set cores2 ""

        foreach core $coreList {
            if {$core != "funnel"} {
            if {$cores2 != ""} {
                append cores2 ","
            }
            append cores2 $core
            }
        }

        cores
        srcbits

        trace $cores
        stoponwrap $cores

        if {$cores2 != ""} {
            echo "ts: [ts $cores2]"
            echo "tsdebug: [tsdebug $cores2]"
            echo "tsclock: [tsclock $cores2]"
            echo "tsprescale: [tsprescale $cores2]"
            echo "tsbranch: [tsbranch $cores2]"
            echo "tsitc: [tsitc $cores2]"
            echo "tsowner: [tsowner $cores2]"
            echo "tracemode: [tracemode $cores2]"
            echo "itc: [itc $cores2]"
            echo "maxicnt: [maxicnt $cores2]"
            echo "maxbtm: [maxbtm $cores2]"
        }
    } else {
        echo {Error: Usage: trace [corelist] [on | off | reset | settings | help]}
    }
}

proc wordscollected {core} {
    global te_sinkwp
    global traceBaseAddrArray
    global te_sinkbase_offset

#    echo "wordscollected($core)"

    set tracewp [gettracewp $core]

    switch [string toupper [getSink $core]] {
        "SRAM" { if {$tracewp & 1} {
            set size [getTraceBufferSize $core]
            return "[expr $size / 4] trace words collected"
            }

            return "[expr $tracewp / 4] trace words collected"
        }
        "SBA"  { if {$tracewp & 1} {
            set size [getTraceBufferSize $core]
            return "[expr $size / 4] trace words collected"
            }

            set tracebegin [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
            set traceend $tracewp

            return "[expr ($traceend - $tracebegin) / 4] trace words collected"
        }
    }

    return "unknown trace words collected"
}

proc is_itc_implmented {core} {
    # Caller is responsible for enabling trace before calling this
    # proc, otherwise behavior is undefined

#    echo "is_itc_implmented($core)"

    global itc_traceenable_offset
    global traceBaseAddrArray

    # We'll write a non-zero value to itc_traceenable, verify a
    # non-zero readback, and restore the original value

    set originalval [word [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]
    mww [expr $traceBaseAddrArray($core) + $itc_traceenable_offset] 0xFFFFFFFF
    set readback [word [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]
    set result [expr $readback != 0]
    mww [expr $traceBaseAddrArray($core) + $itc_traceenable_offset] $originalval

    return $result
}

proc get_num_external_trigger_outputs {core} {
    global xto_control_offset
    global traceBaseAddrArray

#    echo "get_num_external_trigger_output($core)"

    # We'll write non-zero nibbles to xto_control, count
    # non-zero nibbles on readback,
    # restore the original xto_control value.  0x1 seems a good
    # nibble to write, that bit is always legal if an external
    # trigger output exists in the nibble slot, regardless of whether other
    # optional trace features are present

    set originalval [word [expr $traceBaseAddrArray($core) + $xto_control_offset]]
    mww [expr $traceBaseAddrArray($core) + $xto_control_offset] 0x11111111
    set readback [word [expr $traceBaseAddrArray($core) + $xto_control_offset]]
    mww [expr $traceBaseAddrArray($core) + $xto_control_offset] $originalval

    set result 0
    for {set i 0} {$i < 8} {incr i} {
        if {($readback & 0xF) == 1} {
            incr result
        }
        set readback [expr $readback >> 4]
    }
    return $result
}

proc get_num_external_trigger_inputs {core} {
    global xti_control_offset
    global traceBaseAddrArray

#    echo "get_num_external_trigger_inputs($core)"

    # We'll write non-zero nibbles to xti_control, count
    # non-zero nibbles on readback,
    # restore the original xti_control value.  2 seems a good
    # nibble to write, that bit is always legal if an external
    # trigger input exists in the nibble slot, regardless of whether other
    # optional trace features are present

    set originalval [word [expr $traceBaseAddrArray($core) + $xti_control_offset]]
    mww [expr $traceBaseAddrArray($core) + $xti_control_offset] 0x22222222
    set readback [word [expr $traceBaseAddrArray($core) + $xti_control_offset]]
    mww [expr $traceBaseAddrArray($core) + $xti_control_offset] $originalval

    set result 0
    for {set i 0} {$i < 8} {incr i} {
        if {($readback & 0xF) == 0x2} {
            incr result
        }
        set readback [expr $readback >> 4]
    }
    return $result
}

# Surprisingly, Jim Tcl lacks a primitive that returns the value of a
# register.  It only exposes a "cooked" line of output suitable for
# display.  But we can use that to extrace the actual register value
# and return it

proc regval {name} {
    set displayval [reg $name]
    set splitval [split $displayval ':']
    set val [lindex $splitval 1]
    return [string trim $val]
}

proc wp_control_set {core bit} {
    global wp_control_offset
    global traceBaseAddresses

#    echo "wp_control_set($core $bit)"

    foreach baseAddress $traceBaseAddresses {
        set newval [expr [word [expr $baseAddress + $wp_control_offset]] | (1 << $bit)]
        mww [expr $baseAddress + $wp_control_offset] $newval
    }
}

proc wp_control_clear {core bit} {
    global wp_control_offset
    global traceBaseAddresses

#    echo "wp_control_clear($core $bit)"

    foreach baseAddress $traceBaseAddresses {
        set newval [expr [word [expr $baseAddress + $wp_control_offset]] & ~(1 << $bit)]
        mww [expr $baseAddress + $wp_control_offset] $newval
    }
}

proc wp_control_get_bit {core bit} {
    global wp_control_offset
    global traceBaseAddresses

#    echo "wp_control_get_bit($core $bit)"

    set baseAddress [lindex $traceBaseAddresses 0]
    return [expr ([word [expr $baseAddress + $wp_control_offset]] >> $bit) & 0x01]
}

proc wp_control {cores {bit ""} {val ""}} {
#    echo "wp_control($cores $bit $val)"

    if {$bit == ""} {
        set bit $cores
        set cores "all"
    }

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo {Error: Usage: wp_control corelist (wpnum [edge | range]) | help}
        return "error"
    }

    if {$bit == "help"} {
        echo "wp_control: set or display edge or range mode for slected watchpoint"
        echo {Usage: wp_control corelist (wpnum [edge | range]) | help}
        echo "  corelist: Comma separated list of core numbers, or 'all'."
        echo "  wpnum:    Watchpoint number (0-31)"
        echo "  edge:     Select edge mode"
        echo "  range:    Select range mode"
        echo "  help:     Display this message"
        echo ""
    } elseif {$val == ""} {
        set rv ""

        foreach core $coreList {
            set b [wp_control_get_bit $core $bit]
            if {$b != 0} {
                set wp "core $core: range"
            } else {
                set wp "core $core: edge"
            }

            if {$rv != ""} {
                append rv "; "
            }

            append rv $wp
        }

        return $rv
    } elseif {$val == "edge"} {
        foreach core $coreList {
            wp_control_clear $core $bit
        }
    } elseif {$val == "range"} {
        foreach core $coreList {
            wp_control_set $core $bit
        }
    } else {
        echo {Error: Usage: wp_control corelist (wpnum [edge | range]) | help}
    }
}

proc xti_action_read {core idx} {
    global xti_control_offset
    global traceBaseAddrArray

#    echo "xti_action_read($core $idx)"

    return [expr ([word [expr $traceBaseAddrArray($core) + $xti_control_offset]] >> ($idx*4)) & 0xF]
}

proc xti_action_write {core idx val} {
    global xti_control_offset
    global traceBaseAddrArray

#    echo "xti_action_write($core [format 0x%08lx $idx] [format 0x%08lx $val])"

    set zeroed [expr ([word [expr $traceBaseAddrArray($core) + $xti_control_offset]] & ~(0xF << ($idx*4)))]
    set ored [expr ($zeroed | (($val & 0xF) << ($idx*4)))]
    mww [expr $traceBaseAddrArray($core) + $xti_control_offset] $ored
}

proc xto_event_read {core idx} {
    global xto_control_offset
    global traceBaseAddrArray

#    echo "xto_event_read($core $idx)"

    return [expr ([word [expr $traceBaseAddrArray($core) + $xto_control_offset]] >> ($idx*4)) & 0xF]
}

proc xto_event_write {core idx val} {
    global xto_control_offset
    global traceBaseAddrArray

#    echo "xto_even_write($core [format 0x%08lx $idx] [format 0x%08lx $val])"

    set zeroed [expr ([word [expr $traceBaseAddrArray($core) + $xto_control_offset]] & ~(0xF << ($idx*4)))]
    set ored [expr ($zeroed | (($val & 0xF) << ($idx*4)))]
    mww [expr $traceBaseAddrArray($core) + $xto_control_offset] $ored
}

proc xti_action {cores {idx ""} {val ""}} {
#    echo "xti_action($cores $idx $val)"

    if {$idx == ""} {
        set idx $cores
        set cores "all"
    }

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo {Error: Usage: xti_action corelist (xtireg [none | start | stop | record] | help)}
        return "error"
    }

    if {$idx == "help"} {
        echo "xti_action: set or display external trigger input action type (none, start, stop record)"
        echo {Usage: xti_action corelist (xtireg [none | start | stop | record] | help)}
        echo "  corelist: Comma separated list of core numbers, or 'all'."
        echo "  xtireg:   XTI action number (0 - 7)"
        echo "  none:     no action"
        echo "  start:    start tracing"
        echo "  stop:     stop tracing"
        echo "  record:   emot program trace sync message"
        echo "  help:     Display this message"
        echo ""
    } elseif {$val == ""} {
        # display current state of xti reg
        set rv ""

        foreach core $coreList {
            switch [xti_action_read $core $idx] {
                0       { set action "none"   }
                2       { set action "start"  }
                3       { set action "stop"   }
                4       { set action "record" }
                default { set action "reserved" }
            }

            set tsd "core $core: $action"

            if {$rv != ""} {
                append rv "; "
            }

            append rv $tsd
        }

        return $rv
    } else {
        # set current state of xti reg
        set rv ""

        foreach core $coreList {
            switch $val {
                "none"   { set action 0 }
                "start"  { set action 2 }
                "stop"   { set action 3 }
                "record" { set action 4 }
                default  { set action 0 }
            }

            xti_action_write $core $idx $action
        }
        echo -n ""
    }
}

proc init {} {
    global te_control_offset
    global te_sinkrp_offset
    global te_sinkwp_offset
    global traceBaseAddresses
    global caBaseAddresses
    global traceFunnelAddresses
    global traceBaseAddrArray
    global traceFunnelAddrArray
    global CABaseAddrArray
    global num_cores
    global num_funnels
    global has_event
    global have_htm

#    echo "init()"

    if {[info exists traceFunnelAddresses] == 0} {
      set traceFunnelAddresses { 0 }
    }

    # put all cores and funnels in a known state

    setAllTeControls $te_control_offset 0x01830001
    setAllTfControls $te_control_offset 0x00000001

    setAllTeControls $te_sinkrp_offset 0xffffffff
    setAllTfControls $te_sinkrp_offset 0xffffffff

    setAllTeControls $te_sinkwp_offset 0x00000000
    setAllTfControls $te_sinkwp_offset 0x00000000

    set core 0

    foreach addr $traceBaseAddresses {
        set traceBaseAddrArray($core) $addr
        setSink $core "SRAM"
        incr core
    }

    set num_cores $core
    set core 0

    foreach addr $caBaseAddresses {
        set CABaseAddrArray($core) $addr
        incr core
    }

	# find the master funnel. If there is only one funnel, it will be it. Otherwise
	# look at all funnel destinations supported for each funnel
	
	# the idea is we will set all funnels except master to feed into the master
	
	set num_funnels 0
	
	if {($traceFunnelAddresses != 0) && ($traceFunnelAddresses != "")} {
		foreach addr $traceFunnelAddresses {
			if {[hasFunnelSink $addr] != 0} {
				set traceFunnelAddrArray($num_funnels) $addr
				incr num_funnels
			} else {
				set traceBaseAddrArray(funnel) $addr
			}
		}
		
		# num_funnels is the total number of funnels, including the master!!
		
		incr num_funnels

        setFunnelSinks "SRAM"
    }
		
    set have_htm [checkHaveHTM]

    # see if we have an evControl reg

    set has_event [checkHaveEvent]
    
    setTraceBufferWidth

    echo -n ""
}

# following routines are used durring script debug, or just to find out the state of things

proc qts {{cores "all"}} {
    global ts_control_offset
    global traceBaseAddrArray

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qts [<cores>]"
        return "error"
    }

    # display current status of ts enable
    set rv ""

    foreach core $coreList {
        if {$core != "funnel"} {
            set tse "core $core: "

            lappend tse [wordhex [expr $traceBaseAddrArray($core) + $ts_control_offset]]

            if {$rv != ""} {
                append rv "; $tse"
            } else {
                set rv $tse
            }
        }
    }

    return $rv
}

proc qte {{cores "all"}} {
    global te_control_offset
    global traceBaseAddrArray

    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qte [<cores>]"
        return "error"
    }

    # display current status of ts enable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_control_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

proc qtw {{cores "all"}} {
    global te_sinkwp_offset
    global traceBaseAddrArray

    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qtw [<cores>]"
        return "error"
    }

    # display current status of ts enable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

proc qtb {{cores "all"}} {
    global te_sinkbase_offset
    global traceBaseAddrArray

    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qtb [<cores>]"
        return "error"
    }

    # display current status of ts enable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

proc qtl {{cores "all"}} {
    global te_sinklimit_offset
    global traceBaseAddrArray

    set coreList [parseCoreFunnelList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qte [<cores>]"
        return "error"
    }

    # display current status of ts enable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

proc qitctraceenable {{cores "all"}} {
    global traceBaseAddrArray
    global itc_traceenable_offset

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qitctraceenable [corelist]"
        return "error"
    }

    # display current status of itctraceenable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

proc qitctriggerenable {{cores "all"}} {
    global traceBaseAddrArray
    global itc_triggerenable_offset

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: qitctriggerenable [corelist]"
        return "error"
    }

    # display current status of itctriggerenable
    set rv ""

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_triggerenable_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    return $rv
}

# following routines are used for debugging

# dump trace registers (at least some of them)

proc dts {{cores "all"}} {
    global traceBaseAddrArray
    global traceFunnelAddrArray
    global te_control_offset
    global te_impl_offset
    global ts_control_offset
    global ts_lower_offset
    global ts_upper_offset

    global num_funnels 
    global has_event

	if {$num_funnels > 0} {
    	set coreList [parseCoreFunnelList $cores]
	} else {
    	set coreList [parseCoreList $cores]
    }

    if {$coreList == "error"} {
        echo "Error: Usage: dts [corelist]"
        return "error"
    }

    set doFunnel 0
    
	foreach core $coreList {
	    if {$core == "funnel"} {
	        set doFunnel 1
	    } else {
	        echo "core: $core"
	        set tsc [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
	        set tsch [format "0x%04x" $tsc]
       		set tsw [expr ($tsc >> 24)]
       		echo "ts_control: $tsch, ts width: $tsw"
	        set tsl [word [expr $traceBaseAddrArray($core) + $ts_lower_offset]]
	        set tsu [word [expr $traceBaseAddrArray($core) + $ts_upper_offset]]
	        set tslh [format "%04x" $tsl]
	        set tsuh [format "%04x" $tsu]
	        echo "ts: 0x$tsu$tsl"
	    }
	}
	
	    if {$num_funnels > 0} {
        for {set i 0} {$i < ($num_funnels - 1)} {incr i} {
            setFunnelSink $traceFunnelAddrArray($i) "funnel"
        }
    
        setFunnelSink $traceBaseAddrArray(funnel) "SRAM"
    }
	
	if {$doFunnel != 0} {
		if {$num_funnels > 0} {
	        for {set i 0} {$i < ($num_funnels - 1)} {incr i} {
	        	set fa [format "0x%08x" $traceFunnelAddrArray($i)]
			    echo "funnel @ $fa:"
    		    set tsc [word [expr $traceFunnelAddrArray($i) + $ts_control_offset]]
        		set tsch [format "0x%04x" $tsc]
        		set tsw [expr ($tsc >> 24)]
        		echo "ts_control: $tsch, ts width: $tsw"
        		set tsl [word [expr $traceFunnelAddrArray($i) + $ts_lower_offset]]
        		set tsu [word [expr $traceFunnelAddrArray($i) + $ts_upper_offset]]
        		set tslh [format "%04x" $tsl]
        		set tsuh [format "%04x" $tsu]
        		echo "ts: 0x$tsu$tsl"
        	}
    
		}

       	set fa [format "0x%08x" $traceBaseAddrArray(funnel)]
	    echo "master funnel @ $fa:"
        set tsc [word [expr $traceBaseAddrArray(funnel) + $ts_control_offset]]
        set tsch [format "0x%04x" $tsc]
   		set tsw [expr ($tsc >> 24)]
   		echo "ts_control: $tsch, ts width: $tsw"
        set tsl [word [expr $traceBaseAddrArray(funnel) + $ts_lower_offset]]
        set tsu [word [expr $traceBaseAddrArray(funnel) + $ts_upper_offset]]
        set tslh [format "%04x" $tsl]
        set tsuh [format "%04x" $tsu]
        echo "ts: 0x$tsu$tsl"
	}
}

proc dtr {{cores "all"}} {
    global traceBaseAddrArray
    global te_control_offset
    global ev_control_offset
    global te_impl_offset
    global te_sinkbase_offset
    global te_sinkbasehigh_offset
    global te_sinklimit_offset
    global te_sinkwp_offset
    global te_sinkrp_offset
    global te_sinkdata_offset

    global xti_control_offset
    global xto_control_offset
    global itc_traceenable_offset
    global itc_trigenable_offset
    global ts_control_offset

    global num_funnels 
    global has_event

    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        echo "Error: Usage: dtr [corelist]"
        return "error"
    }

    # display current status of teenable
  
    set rv "teControl: "

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_control_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "teImpl:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_impl_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "te_sinkBase:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "te_sinkBaseHigh:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "te_sinkLimit:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "te_sinkwp:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "te_sinkrp:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $te_sinkrp_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "itcTraceEnable:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "itcTrigEnable:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $itc_trigenable_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "tsControl:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $ts_control_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    if {$has_event != 0} {
        set rv "evControl:"

        foreach core $coreList {
            set tse "core $core: "

            lappend tse [wordhex [expr $traceBaseAddrArray($core) + $ev_control_offset]]

            if {$rv != ""} {
                append rv "; $tse"
            } else {
                set rv $tse
            }
        }

        echo "$rv"
    }

    set rv "xtiControl:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $xti_control_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    set rv "xtoControl:"

    foreach core $coreList {
        set tse "core $core: "

        lappend tse [wordhex [expr $traceBaseAddrArray($core) + $xto_control_offset]]

        if {$rv != ""} {
            append rv "; $tse"
        } else {
            set rv $tse
        }
    }

    echo "$rv"

    if {$num_funnels > 0} {
        # display current status of funnel regs
  
        set rv "tfControl: "

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_control_offset]]

        append rv $tse

        echo "$rv"

        set rv "tfImpl: "

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_impl_offset]]

    append rv $tse

        echo "$rv"

        set rv "tfSinkBase: "

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_sinkbase_offset]]

    append rv $tse

        echo "$rv"

        set rv "tfSinkBaseHigh: "

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_sinkbasehigh_offset]]

    append rv $tse

        echo "$rv"

        set rv "tfSinkLimit:"

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_sinklimit_offset]]

    append rv $tse

        echo "$rv"

        set rv "tfSinkwp:"

    set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_sinkwp_offset]]

    append rv $tse

        echo "$rv"

        set rv "tfSinkrp:"

        set tse [wordhex [expr $traceBaseAddrArray(funnel) + $te_sinkrp_offset]]

    append rv $tse

        echo "$rv"
    }
}

# program chip to collect a trace in the sba buffer

proc sba {{addr ""} {size ""}} {
    if {($addr == "") || ($size == "")} {
        echo "Useage: sba addr size"
    } else {
        global verbose
        set verbose 2
        stoponwrap 0 on
        setTeStallEnable 0 on
        set htm [checkHaveHTM]
        if {$htm != 0} {
            tracemode 0 htm
        } else {
            tracemode 0 btm
        }
        tracedst 0 sba $addr $size
            cleartrace 0
        trace 0 on
    }
}

# program chip to collect a trace in the sram buffer

proc sram {} {
    global verbose
    set verbose 2
    stoponwrap 0 on
    setTeStallEnable 0 on
    set htm [checkHaveHTM]
    if {$htm != 0} {
        tracemode 0 htm
    } else {
        tracemode 0 btm
    }
    tracedst 0 sram
    cleartrace 0
    trace 0 on
}

proc sample {} {
    global verbose

#	echo "sample()"

    set verbose 2
    stoponwrap 0 on
    setTeStallEnable 0 on
    tracemode 0 sample
    tracedst 0 sram
    cleartrace
    trace on
}

proc tsallon {} {
#	echo "tsallon()"

    ts all on
    tsbranch all all
    tsitc all on
    tsowner all on
}

proc haspcsampling {core} {
    global traceBaseAddrArray
    global pcs_control_offset

#    echo "haspcsamping($core)"

    set current [word [expr $traceBaseAddrArray($core) + $pcs_control_offset]]

    mww [expr $traceBaseAddrArray($core) + $pcs_control_offset] 0x1
    set t [word [expr $traceBaseAddrArray($core) + $pcs_control_offset]]

    mww [expr $traceBaseAddrArray($core) + $pcs_control_offset] $current

    if {($t & 0x1) != 0} {
        return "true"
    }

    return "false"
}

proc enablepcsampling {core size} {
    global traceBaseAddrArray
    global pcs_control_offset
    global pcs_sample

#    echo "enablepcsampoing($core [format 0x%08lx $size])"

    set samp_ofs [expr $pcs_sample - $size + 4]

    riscv memory_sample ${core} [expr $traceBaseAddrArray($core) + $samp_ofs] $size
    mww [expr $traceBaseAddrArray($core) + $pcs_control_offset] 0x1
}

proc disablepcsampling {core} {
    global traceBaseAddrArray
    global pcs_control_offset

#    echo "disablepcsampling($core)"

    mww [expr $traceBaseAddrArray($core) + $pcs_control_offset] 0x0
}

proc fetchsampledata {fn} {
    #echo "fetching sample data to $fn"

#    echo "fetchsampledata($fn)"

    set fp [open $fn w]
    set result [riscv dump_sample_buf base64]
    puts -nonewline $fp $result
    close $fp
}

init
tracedst
echo -n ""

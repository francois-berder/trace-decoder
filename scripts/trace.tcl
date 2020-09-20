#
# Scripts for trace using OpenOCD
#


#set traceBaseAddresses {0x20007000 0x20008000}
#set traceFunnelAddress 0x20009000
#set traceBaseAddresses 0x10000000
#set traceFunnelAddress 0x00000000
#set caBaseAddresses 0x1000f000

set te_control_offset      0x00
set te_impl_offset         0x04
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
set te_upper_offset        0x48
set xti_control_offset     0x50
set xto_control_offset     0x54
set wp_control_offset      0x58
set itc_traceenable_offset 0x60
set itc_trigenable_offset  0x64

set ca_control_offset   0x00
set ca_impl_offset      0x04
set ca_sink_wp_offset   0x1c
set ca_sink_rp_offset   0x20
set ca_sink_data_offset 0x24

set num_cores  0
set has_funnel 0
set have_htm 0

set trace_buffer_width 0

set traceBufferAddr 0x00000000

set verbose 0

# local helper functions not intented to be called directly

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

    foreach controlReg $traceBaseAddresses {
	mww [expr $controlReg + $offset] $val
    }
}

proc setAllTfControls {offset val} {
    global traceFunnelAddress

    if {$traceFunnelAddress != 0} {
	mww [expr $traceFunnelAddress + $offset] $val
    }
}

# Returns list of all cores and funnel if present

proc getAllCoreFunnelList {} {
    global traceBaseAddresses
    global traceFunnelAddress

    set cores {}
    set index 0

    foreach controlReg $traceBaseAddresses {
	lappend cores $index
	set index [expr $index + 1]
    }

    if {$traceFunnelAddress != "0x00000000" && $traceFunnelAddress != ""} {
	lappend cores funnel
    }

    return $cores
}

# Returns list of all cores (but not funnel)

proc getAllCoreList {} {
    global traceBaseAddresses
    global traceFunnelAddress

    set cores {}
    set index 0

    foreach controlReg $traceBaseAddresses {
	lappend cores $index
	set index [expr $index + 1]
    }

    return $cores
}

# returns a list struct from parsing $cores with each element a core id

proc parseCoreFunnelList {cores} {
    global num_cores
    global has_funnel

    # parse core and build list of cores

    if {$cores == "all" || $cores == ""} {
	return [getAllCoreFunnelList]
    }

    set t [split $cores ","]

    foreach core $t {
	if {$core == "funnel"} {
	    # only accept funnel if one is present

	    if {$has_funnel == 0} {
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

proc checkHaveHTM {} {
    global traceBaseAddresses
    global te_control_offset
	global verbose

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

# ite = [i]s [t]race [e]nabled
proc ite {} {
    global te_control_offset
    global traceBaseAddresses
    global traceFunnelAddress

    set rc 0

    foreach baseAddress $traceBaseAddresses {
		set tracectl [word [expr $baseAddress + $te_control_offset]]
		if {($tracectl & 0x6) != 0} {
			return 1
		}
    }

    if {$traceFunnelAddress != 0} {
		set tracectl [word [expr $traceFunnelAddress + $te_control_offset]]
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
    global has_funnel
    global trace_buffer_width

    if {$has_funnel != 0} {
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

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {($tracectl & 0x2) != 0} {
		return "on"
    }

    return "off"
}

proc getTracingEnable {core} {
    global traceBaseAddrArray
    global te_control_offset

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {($tracectl & 0x4) != 0} {
		return "on"
    }

    return "off"
}

proc clearAndEnableTrace { core } {
	cleartrace $core
	enableTraceEncoder $core
}

proc enableTraceEncoder {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t | 0x00000003]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc enableTracing {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t | 0x00000005]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc disableTraceEncoder {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x00000002]
    set t [expr $t | 0x00000001]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc disableTracing {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x00000004]
    set t [expr $t | 0x00000001]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc resetTrace {core} {
    global traceBaseAddrArray
    global te_control_offset

    mww [expr $traceBaseAddrArray($core) + $te_control_offset] 0
    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x00000001]
    set t [expr $t | 0x00000001]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getSinkError {core} {
    global traceBaseAddrArray
    global te_control_offset

    set tracectl [word [expr $traceBaseAddrArray($core) + $te_control_offset]]

    if {(($tracectl >> 27) & 1) != 0} {
		return "Error"
    }

    return "Ok"
}

proc clearSinkError {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t | (1 << 27)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc isTsEnabled {core} {
    global traceBaseAddrArray
    global ts_control_offset

    if {$core != "funnel"} {
		set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
	if {[expr $tsctl & 0x00008003] == 0x00008003} {
	    return "on"
	}

	return "off"
    }
}

proc enableTs {core} {
    global traceBaseAddrArray
    global ts_control_offset

    if {$core != "funnel"} {
		set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
		set tsctl [expr $tsctl | 0x00008003]
		mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
    }
}

proc disableTs {core} {
    global traceBaseAddrArray
    global ts_control_offset

    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set tsctl [expr $tsctl & ~0x00008001]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
}

proc resetTs {core} {
    global traceBaseAddrArray
    global ts_control_offset

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

    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    if {[expr $tsctl & 0x00000008] != 0} {
		return "on"
    }

    return "off"
}

proc getTsLower {core} {
    global traceBaseAddrArray
    global ts_lower_offset

    return [format 0x%08x [word [expr $traceBaseAddrArray($core) + $ts_lower_offset]]]
}

proc enableTsDebug {core} {
    global traceBaseAddrArray
    global ts_control_offset

    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set tsctl [expr $tsctl | 0x0000008]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
}

proc disableTsDebug {core} {
    global traceBaseAddrArray
    global ts_control_offset

    set tsctl [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set tsctl [expr $tsctl & ~0x0000008]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $tsctl
}

proc getTsClockSrc {core} {
    global traceBaseAddrArray
    global ts_control_offset

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

# Note: ts clock src is read only and cannot be set

#proc setTsClockSrc {core clock} {
#  global traceBaseAddrArray
#  global ts_control_offset
#
#  switch $clock {
#  "none"     { set src 0 }
#  "external" { set src 1 }
#  "bus"      { set src 2 }
#  "core"     { set src 3 }
#  "slave"    { set src 4 }
#  default    { set src 0 }
#  }
#
#  set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
#  set t [expr $t & ~0x0070]
#  set t [expr $t | ($src << 4)]
#  mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
#}

proc getTsPrescale {core} {
    global traceBaseAddrArray
    global ts_control_offset

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

    switch $prescl {
		1       { set ps 0 }
		4       { set ps 1 }
		16      { set ps 2 }
		64      { set ps 3 }
		default { set ps 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $ts_control_offset]]
    set t [expr $t & ~0x0300]
    set t [expr $t | ($ps << 8)]
    mww [expr $traceBaseAddrArray($core) + $ts_control_offset] $t
}

proc getTsBranch {core} {
    global traceBaseAddrArray
    global ts_control_offset

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

    switch $branch {
		"off"                { set br 0 }
		"indirect+exception" { set br 1 }
		"reserved"           { set br 1 }
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

    switch $wrap {
		"on"    { set sow 1 }
		"off"   { set sow 0 }
		default { set sow 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x4000]
    set t [expr $t | ($sow << 14)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getTeStopOnWrap {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 14) & 0x1]

    switch $t {
		0 { return "off"  }
		1 { return "on"  }
    }
}

proc getCAStopOnWrap {core} {
    global CABaseAddrArray
    global ca_control_offset

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

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 13) & 0x1]

    switch $t {
		0 { return "off"  }
		1 { return "on"  }
    }
}

proc setTraceMode { core usermode } {
	global have_htm

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
		#"events" {}
	}
}

proc setTargetTraceMode {core mode} {
    global traceBaseAddrArray
    global te_control_offset

	#echo "Setting target trace mode to $mode"
    switch $mode {
       "none"       { set tm 0 }
       "sample"     { set tm 1 }
       "btm+sync"   { set tm 3 }
       "btm"        { set tm 3 }
       "htmc+sync"  { set tm 6 }
       "htmc"       { set tm 6 }
       "htm+sync"   { set tm 7 }
       "htm"        { set tm 7 }
       default      { set tm 0 }
    }

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x0070]
    set t [expr $t | ($tm << 4)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getTraceMode {core} {
	set tm [getTargetTraceMode $core]
	switch $tm {
       "none"       { return "off" }
       "sample"     { return "sample" }
       "btm+sync"   { return "instruction" }
       "htmc+sync"  { return "instruction" }
       "htm+sync"   { return "instruction" }
	}
	return "off"
}

proc getTargetTraceMode {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 4) & 0x7]

    switch $t {
       0       { return "none" }
       1       { return "sample" }
       3       { return "btm+sync"  }
       6       { return "htmc+sync" }
       7       { return "htm+sync"  }
       default { return "reserved" }
    }
}

proc setITC {core mode} {
    global traceBaseAddrArray
    global te_control_offset

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

    mww [expr $traceBaseAddrArray($core) + $itc_traceenable_offset] [expr $mask]
}

proc getITCMask {core} {
    global traceBaseAddrArray
    global itc_traceenable_offset

    set t [wordhex [expr $traceBaseAddrArray($core) + $itc_traceenable_offset]]

    return $t
}

proc setITCTriggerMask {core mask} {
    global traceBaseAddrArray
    global itc_trigenable_offset

    mww [expr $traceBaseAddrArray($core) + $itc_trigenable_offset] [expr $mask]
}

proc getITCTriggerMask {core} {
    global traceBaseAddrArray
    global itc_trigenable_offset

    set t [wordhex [expr $traceBaseAddrArray($core) + $itc_trigenable_offset]]

    return $t
}

proc setMaxIcnt {core maxicnt} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0xf00000]
    set t [expr $t | ($maxicnt << 20)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getMaxIcnt {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & 0xf00000]
    set t [expr $t >> 20]

    return $t
}

proc findMaxICnt { core }  {
	# Start at 15 and work down until one sticks.
	for {set x 15} { $x > 0 } {set x [expr {$x - 1}]} {
		setMaxIcnt $core $x
		set y [getMaxIcnt $core]
		if {$x == $y} {
			return $x;
		}
	}
}
proc setMaxBTM {core maxicnt} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & ~0x0f0000]
    set t [expr $t | ($maxicnt << 16)]
    mww [expr $traceBaseAddrArray($core) + $te_control_offset] $t
}

proc getMaxBTM {core} {
    global traceBaseAddrArray
    global te_control_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr $t & 0x0f0000]
    set t [expr $t >> 16]

    return $t
}

# helper functions used during debugging ot script

proc srcbits {} {
    global te_impl_offset
    global traceBaseAddresses

    set numbits [expr [word [expr [lindex $traceBaseAddresses 0] + $te_impl_offset]] >> 24 & 7]
    return $numbits
}

# global functions intended to be called from command line or from freedom studio

proc printtracebaseaddresses {} {
    global traceBaseAddresses
    global traceFunnelAddress

    set core 0

    foreach baseAddress $traceBaseAddresses {
        echo "core $core: trace block at $baseAddress"
        set core [expr $core + 1]
    }

    if {$traceFunnelAddress != 0} {
        echo "Funnel block at $traceFunnelAddress"
    }

    echo -n ""
}

proc getTeVersion {core} {
    global te_impl_offset
    global traceBaseAddresses

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

proc tracemode {{cores "all"} {opt ""}} {
    set coreList [parseCoreList $cores]

    if {$coreList == "error"} {
        if {$opt == ""} {
            set opt $cores
            set cores "all"

            set coreList [parseCoreList $cores]
        }

        if {$coreList == "error"} {
            echo {Error: Usage: tracemode [corelist] [none | all | btm | htm | htmc | sample | help]}
            return "error"
        }
    }

    if {$opt == ""} {
        # display current status of ts enable
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
        echo {Usage: tracemode [corelist] [none | all | btm | htm | htmc | sample | help]}
        echo "  corelist: Comma separated list of core numbers, or 'all'. Not specifying is equivalent to all"
        echo "  btm:      Generate both sync and btm trace messages"
        echo "  htm:      Generate sync and htm trace messages (with return stack optimization or repeat branch optimization)"
        echo "  htmc      Generate sync and conservitive htm trace messages (without return stack optimization or repeat branch optimization)"
	echo "  sample    Generate PC sample trace using In Circuit Trace mode"
        echo "  all:      Generate both sync and btm or htm trace messages (whichever is supported by hardware)"
        echo "  none:     Do not generate sync or btm trace messages"
        echo "  help:     Display this message"
        echo ""
        echo "tracemode with no arguments will display the current setting for the type"
        echo "of messages to generate (none, sync, or all)"
        echo ""
    } elseif {($opt == "sample") || ($opt == "all") || ($opt == "none") || ($opt == "btm") || ($opt == "htm") || ($opt == "htmc") || ($opt == "btm+sync") || ($opt == "htm+sync") || ($opt == "htmc+sync")} {
        foreach core $coreList {
            setTargetTraceMode $core $opt
        }
        echo -n ""
    } else {
        echo {Error: Usage: tracemode [corelist] [all | btm | htm | htmc | none | sample | help]}
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

    mww [expr $traceBaseAddrArray($core) + $te_sinkrp_offset] $ptr
}

proc setcareadptr {core ptr} {
    global CABaseAddrArray
    global ca_sink_rp_offset

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

    return [word [expr $CABaseAddrArray($core) + $ca_sink_data_offset]]
}

proc writeSRAM {core file limit} {
    global verbose
	
    if { $verbose > 1 } {
        echo ""
    }

	set tracewp [gettracewp $core]

    if {$file == "stdout"} {
		if {($tracewp & 1) == 0 } { 
			# buffer has not wrapped
			set tracebegin 0
			set traceend $tracewp

			if {$limit > 0} {
				set stop_on_wrap [getTeStopOnWrap $core]
				set length [expr $traceend - $tracebegin]
				if {$length > $limit} {
					if {$stop_on_wrap=="on"} {
						# use the beginning of the buffer by
						# adjusting the end point.
						set traceend [expr $tracebegin + $limit]
					} else {
						# use the end of the buffer by adjusting 
						# the begin point
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if { $verbose > 1 } {
				echo "Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], nowrap, [expr $traceend - $tracebegin] bytes"
			}

			setreadptr $core 0

			set f ""

			for {set i 0} {$i < $traceend} {incr i 4} {
				set w [format %08x [eval readSRAMData $core]]
				append f $w
			}
		} else {
			if { $verbose > 1 } {
				echo "Trace wrapped"
			}
			set tracebegin [expr $tracewp & 0xfffffffe]
			set traceend [getTraceBufferSize $core]

			set tracebegin 0
			set traceend [expr $tracewp & 0xfffffffe]

			set do1 1
			set do2 1

			if {$limit > 0} {
				set stop_on_wrap [getTeStopOnWrap $core]
				set length1 [expr $traceend - $tracebegin]
				set length2 [expr $traceend2 - $tracebegin2]
				if {$stop_on_wrap=="on"} {
					#use the beginning of the buffer
					if { $limit < $length1 } {
						# everything is in part1, just need
						# adjust the endpoint
						set traceend [expr $tracebegin + $limit]
						# don't do part 2
						set do2 0
					} else {
						# need all of part 1, and part of part 2
						set limit [expr $limit - $length1]
						set traceend2 [expr $tracebegin2 + $limit]
					}
				} else {
					#use the end of the buffer
					if { $length2 > $limit } {
						# only need to do part 2
						set tracebegin2 [expr $traceend2 - $limit]
						set do1 0
					} else {
						# need to do the end of part 1 
						# and all of part 2
						set limit [expr $limit - $length2]
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if {$do1 == 1} {
				if { $verbose > 1 } {
					echo "Trace from [format 0x%08x $tracebegin] to [format %08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setreadptr $core $tracebegin

				set f ""

				for {set i $tracebegin} {$i < $traceend} {incr i 4} {
					set w [format %08x [eval readSRAMData $core]]
					append f $w
				}
			}

			if {$do2 == 1} {
				if { $verbose > 1 } {
					echo "Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setreadptr $core 0

				for {set i $tracebegin} {$i < $traceend} {incr i 4} {
					set w [format %08x [eval readSRAMData $core]]
					append f $w
				}
			}
		}


		return $f
    } else {
		set fp [open "$file" wb]

		if {($tracewp & 1) == 0 } { 
			# buffer has not wrapped
			set tracebegin 0
			set traceend $tracewp

			if {$limit > 0} {
				set stop_on_wrap [getTeStopOnWrap $core]
				set length [expr $traceend - $tracebegin]
				if {$length > $limit} {
					if {$stop_on_wrap == "on"} {
						# use the beginning of the buffer by
						# adjusting the end point.
						set traceend [expr $tracebegin + $limit]
					} else {
						# use the end of the buffer by adjusting 
						# the begin point
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if { $verbose > 1 } {
				echo "Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], nowrap, [expr $traceend - $tracebegin] bytes"
			}
			setreadptr $core 0

			writeSRAMdata $core $tracebegin $traceend $fp
		} else {
			if { $verbose > 1 } {
				echo "Trace wrapped"
			}

			set tracebegin [expr $tracewp & 0xfffffffe]
			set traceend [getTraceBufferSize $core]

			set tracebegin2 0
			set traceend2 [expr $tracewp & 0xfffffffe]

			set do1 1
			set do2 1

			if {$limit > 0} {
				set stop_on_wrap [getTeStopOnWrap $core]
				set length1 [expr $traceend - $tracebegin]
				set length2 [expr $traceend2 - $tracebegin2]
				if {$stop_on_wrap == "on"} {
					#use the beginning of the buffer
					if { $limit < $length1 } {
						# everything is in part1, just need
						# adjust the endpoint
						set traceend [expr $tracebegin + $limit]
						# don't do part 2
						set do2 0
					} else {
						# need all of part 1, and part of part 2
						set limit [expr $limit - $length1]
						set traceend2 [expr $tracebegin2 + $limit]
					}
				} else {
					#use the end of the buffer
					if { $length2 > $limit } {
						# only need to do part 2
						set tracebegin2 [expr $traceend2 - $limit]
						set do1 0
					} else {
						# need to do the end of part 1 
						# and all of part 2
						set limit [expr $limit - $length2]
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if {$do1 == 1} {
				if { $verbose > 1 } {
					echo "Trace from [format 0x%08x $tracebegin] to [format %08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setreadptr $core $tracebegin
				writeSRAMdata $core $tracebegin $traceend $fp
			}


			if {$do2 == 1} {
				if { $verbose > 1 } {
					echo "Trace from [format 0x%08x $tracebegin2] to [format 0x%08x $traceend2], [expr $traceend2 - $tracebegin2] bytes"
				}
				setreadptr $core 0
				writeSRAMdata $core $tracebegin2 $traceend2 $fp
			}
		}

		close $fp
    }
}

proc writeCASRAM {core file limit} {
    global verbose
	
    if { $verbose > 1 } {
        echo ""
    }

    set tracewp [getcatracewp $core]

    if {$file == "stdout"} {
		if {($tracewp & 1) == 0 } { 
			# buffer has not wrapped
			set tracebegin 0
			set traceend $tracewp

			if {$limit > 0} {
				set stop_on_wrap [getCAStopOnWrap $core]
				set length [expr $traceend - $tracebegin]
				if {$length > $limit} {
					if {$stop_on_wrap=="on"} {
						# use the beginning of the buffer by
						# adjusting the end point.
						set traceend [expr $tracebegin + $limit]
					} else {
						# use the end of the buffer by adjusting 
						# the begin point
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if { $verbose > 1 } {
				echo "CA Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], nowrap, [expr $traceend - $tracebegin] bytes"
			}

			setcareadptr $core 0

			set f ""

			for {set i 0} {$i < $traceend} {incr i 4} {
				set w [format %08x [eval readCASRAMData $core]]
				append f $w
			}
		} else {
			if { $verbose > 1 } {
				echo "CA Trace wrapped"
			}
			set tracebegin [expr $tracewp & 0xfffffffe]
			set traceend [getCATraceBufferSize $core]

			set tracebegin 0
			set traceend [expr $tracewp & 0xfffffffe]

			set do1 1
			set do2 1

			if {$limit > 0} {
				set stop_on_wrap [getCAStopOnWrap $core]
				set length1 [expr $traceend - $tracebegin]
				set length2 [expr $traceend2 - $tracebegin2]
				if {$stop_on_wrap=="on"} {
					#use the beginning of the buffer
					if { $limit < $length1 } {
						# everything is in part1, just need
						# adjust the endpoint
						set traceend [expr $tracebegin + $limit]
						# don't do part 2
						set do2 0
					} else {
						# need all of part 1, and part of part 2
						set limit [expr $limit - $length1]
						set traceend2 [expr $tracebegin2 + $limit]
					}
				} else {
					#use the end of the buffer
					if { $length2 > $limit } {
						# only need to do part 2
						set tracebegin2 [expr $traceend2 - $limit]
						set do1 0
					} else {
						# need to do the end of part 1 
						# and all of part 2
						set limit [expr $limit - $length2]
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if {$do1 == 1} {
				if { $verbose > 1 } {
					echo "CA Trace from [format 0x%08x $tracebegin] to [format %08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setcareadptr $core $tracebegin

				set f ""

				for {set i $tracebegin} {$i < $traceend} {incr i 4} {
					set w [format %08x [eval readCASRAMData $core]]
					append f $w
				}
			}

			if {$do2 == 1} {
				if { $verbose > 1 } {
					echo "CA Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setcareadptr $core 0

				for {set i $tracebegin} {$i < $traceend} {incr i 4} {
					set w [format %08x [eval readCASRAMData $core]]
					append f $w
				}
			}
		}

		return $f
    } else {
		set fp [open "$file" wb]

		if {($tracewp & 1) == 0 } { 
			# buffer has not wrapped
			set tracebegin 0
			set traceend $tracewp

			if {$limit > 0} {
				set stop_on_wrap [getCAStopOnWrap $core]
				set length [expr $traceend - $tracebegin]
				if {$length > $limit} {
					if {$stop_on_wrap == "on"} {
						# use the beginning of the buffer by
						# adjusting the end point.
						set traceend [expr $tracebegin + $limit]
					} else {
						# use the end of the buffer by adjusting 
						# the begin point
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if { $verbose > 1 } {
				echo "CA Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], nowrap, [expr $traceend - $tracebegin] bytes"
			}
			setcareadptr $core 0

			writeCASRAMdata $core $tracebegin $traceend $fp
		} else {
			if { $verbose > 1 } {
				echo "CA Trace wrapped"
			}

			set tracebegin [expr $tracewp & 0xfffffffe]
			set traceend [getCATraceBufferSize $core]

			set tracebegin2 0
			set traceend2 [expr $tracewp & 0xfffffffe]

			set do1 1
			set do2 1

			if {$limit > 0} {
				set stop_on_wrap [getCAStopOnWrap $core]
				set length1 [expr $traceend - $tracebegin]
				set length2 [expr $traceend2 - $tracebegin2]
				if {$stop_on_wrap == "on"} {
					#use the beginning of the buffer
					if { $limit < $length1 } {
						# everything is in part1, just need
						# adjust the endpoint
						set traceend [expr $tracebegin + $limit]
						# don't do part 2
						set do2 0
					} else {
						# need all of part 1, and part of part 2
						set limit [expr $limit - $length1]
						set traceend2 [expr $tracebegin2 + $limit]
					}
				} else {
					#use the end of the buffer
					if { $length2 > $limit } {
						# only need to do part 2
						set tracebegin2 [expr $traceend2 - $limit]
						set do1 0
					} else {
						# need to do the end of part 1 
						# and all of part 2
						set limit [expr $limit - $length2]
						set tracebegin [expr $traceend - $limit]
					}
				}
			}

			if {$do1 == 1} {
				if { $verbose > 1 } {
					echo "CA Trace from [format 0x%08x $tracebegin] to [format %08x $traceend], [expr $traceend - $tracebegin] bytes"
				}

				setcareadptr $core $tracebegin
				writeCASRAMdata $core $tracebegin $traceend $fp
			}


			if {$do2 == 1} {
				if { $verbose > 1 } {
					echo "CA Trace from [format 0x%08x $tracebegin2] to [format 0x%08x $traceend2], [expr $traceend2 - $tracebegin2] bytes"
				}
				setcareadptr $core 0
				writeCASRAMdata $core $tracebegin2 $traceend2 $fp
			}
		}

		close $fp
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
	if {$length>0} {
		set daddr  [expr $traceBaseAddrArray($core) + $te_sinkdata_offset]
		set data [riscv repeat_read $length $daddr 4] 
		set packed ""

		foreach value [split $data "\r\n "] {
			if {$value != ""} {
				pack w 0x$value -intle 32
				append packed $w
			}
		}
		puts $fp $packed
	}
}

proc writeCASRAMdata { core tracebegin traceend fp } {
    global CABaseAddrArray
    global ca_sink_data_offset

	set length [expr ($traceend - $tracebegin) / 4]
	if {$length>0} {
		set daddr  [expr $CABaseAddrArray($core) + $ca_sink_data_offset]
		set data [riscv repeat_read $length $daddr 4] 
		set packed ""

		foreach value [split $data "\r\n "] {
			if {$value != ""} {
				pack w 0x$value -intle 32
				append packed $w
			}
		}
		puts $fp $packed
	}
}

proc getCapturedTraceSize { core } {
    global has_funnel
    global num_cores
    global verbose

    if {$has_funnel != 0} {
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
    global traceBaseAddrArray
    global tracedBufferSizeArray
    global te_sinkbase_offset
    global te_sinklimit_offset
    global verbose


    #if sink is not a buffer, return

    set fp [open "$file" wb]

    set tracewp [gettracewp $core]

    if { $verbose > 1 } {
        echo ""
    }

    if {($tracewp & 1) == 0 } { 
		# buffer has not wrapped
		set tracebegin [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
		set traceend $tracewp
		if {$limit > 0} {
			set stop_on_wrap [getTeStopOnWrap $core]
			set length [expr $traceend - $tracebegin]
			if {$length > $limit} {
				if {$stop_on_wrap == "on"} {
					# use the beginning of the buffer by
					# adjusting the end point.
					set traceend [expr $tracebegin + $limit]
				} else {
					# use the end of the buffer by adjusting 
					# the begin point
					set tracebegin [expr $traceend - $limit]
				}
			}
		}

		if { $verbose > 1 } {
			echo "Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], nowrap, [expr $traceend - $tracebegin] bytes"
		}

		writeSBAdataX $tracebegin $traceend $fp
	} else {
		if { $verbose > 1 } {
			echo "Trace wrapped"
		}

		set tracebegin [expr $tracewp & 0xfffffffe]
		set traceend [word [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]

		set tracebegin2 [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
		set traceend2 [expr $tracewp & 0xfffffffe] 

		if { $verbose > 1 } {
			echo "part 1: trace beg = [format 0x%08x $tracebegin]"
			echo "        trace end = [format 0x%08x $traceend]"
			echo "part 2: trace beg = [format 0x%08x $tracebegin2]"
			echo "        trace end = [format 0x%08x $traceend2]"
		}

		set do1 1
		set do2 1

		if {$limit > 0} {
			set stop_on_wrap [getTeStopOnWrap $core]
			set length1 [expr $traceend - $tracebegin]
			set length2 [expr $traceend2 - $tracebegin2]
			if  {$stop_on_wrap == "on"} {
				# use the beginning of the buffer
				if { $limit < $length1 } {
					# everything is in part1, just need
					# adjust the endpoint
					set traceend [expr $tracebegin + $limit]
					# don't do part 2
					set do2 0
				} else {
					# need all of part 1, and part of part 2
					set limit [expr $limit - $length1]
					set traceend2 [expr $tracebegin2 + $limit]
				}
			} else {
				# use the end of the buffer
				if { $length2 > $limit } {
					# only need to do part 2
					set tracebegin2 [expr $traceend2 - $limit]
					set do1 0
				} else {
					# need to do the end of part 1 
					# and all of part 2
					set limit [expr $limit - $length2]
					set tracebegin [expr $traceend - $limit]
				}
			}
		}

		if {$do1 == 1} {
			if { $verbose > 1 } {
				echo "Part 1: Trace from [format 0x%08x $tracebegin] to [format 0x%08x $traceend], [expr $traceend - $tracebegin] bytes"
			}

			writeSBAdataX $tracebegin $traceend $fp
		}

		if {$do2 == 1} {
			if { $verbose > 1 } {
				echo "Part 2: Trace from [format 0x%08x $tracebegin2] to [format 0x%08x $traceend2], [expr $traceend2 - $tracebegin2] bytes"
			}

			writeSBAdataX $tracebegin2 $traceend2 $fp
		}
    }
    close $fp
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
			echo [format "Chunk: %08X to %08X" $i [expr $i +$cs]
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
    global has_funnel
    global num_cores
    global verbose

	riscv set_prefer_sba off
    if  { $verbose > 0 } {
		echo -n "collecting trace..."
    }

    if {$has_funnel != 0} {
		set s [getSink funnel]
		switch [string toupper $s] {
			"SRAM" { set f [writeSRAM funnel $file $limit]}
			"SBA" { set f [writeSBA funnel $file $limit]}
		}
		if { $verbose > 0 } {
			echo "done."
		}
		riscv set_prefer_sba on
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
				"SBA" { set f [writeSBA $core $fn $limit]}
				}
			if { $verbose > 0 } {
				echo "done."
			}
		}
		riscv set_prefer_sba on
		if {$file == "stdout"} {
			return $f
		}
    }
}

proc wcab {{file "trace.cat"} {limit 0}} {
    global num_cores
    global verbose

    riscv set_prefer_sba off

    if  { $verbose > 0 } {
        echo -n "collecting CA trace..."
    }

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

        if { $verbose > 0 } {
            echo "done."
        }
    }

    riscv set_prefer_sba on

    if {$file == "stdout"} {
        return $f
    }
}

proc clearTraceBuffer {core} {
    global traceBaseAddrArray
    global te_sinkrp_offset
    global te_sinkwp_offset
    global te_sinkbase_offset

    set s [getSink $core]
    switch [string toupper $s] {
		"SRAM" { 
			mww [expr $traceBaseAddrArray($core) + $te_sinkrp_offset] 0
	    	mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] 0
		}
		"SBA" { 
			set t [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
	    	mww [expr $traceBaseAddrArray($core) + $te_sinkwp_offset] $t
		}
    }
}

proc cleartrace {{cores "all"}} {
    set coreList [parseCoreFunnelList $cores]

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

    echo "[wordhex [expr $traceBaseAddrArray(0) + $ts_control_offset]]"
}

proc gettracewp {core} {
    global te_sinkwp_offset
    global traceBaseAddrArray

    set tracewp [word [expr $traceBaseAddrArray($core) + $te_sinkwp_offset]]

    return $tracewp
}

proc getCASink {core} {
    global CABaseAddrArray
    global ca_control_offset

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

    set tracewp [word [expr $CABaseAddrArray($core) + $ca_sink_wp_offset]]

    return $tracewp
}

proc getSink {core} {
    global traceBaseAddrArray
    global te_control_offset
    global te_impl_offset

    set t [word [expr $traceBaseAddrArray($core) + $te_control_offset]]
    set t [expr ($t >> 28) & 0x0f]

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
    global has_funnel
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
					set teSink "$teSink , size: $size bytes" }
				"SBA"   { 
					set sinkBase [word [expr $traceBaseAddrArray($core) + $te_sinkbase_offset]]
					set sinkLimit [word [expr $traceBaseAddrArray($core) + $te_sinklimit_offset]]
					set sinkBaseHigh [word [expr $traceBaseAddrArray($core) + $te_sinkbasehigh_offset]]
					set sinkBase [expr ($sinkBaseHigh << 32) + $sinkBase]
					set sinkLimit [expr ($sinkBaseHigh << 32) + $sinkLimit]
					set teSink "$teSink ,base: [format 0x%08x $sinkBase], size: [expr $sinkLimit - $sinkBase + $trace_buffer_width] bytes"
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
			if {$has_funnel != 0} {
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
			if {$has_funnel != 0} {
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
			if {$has_funnel != 0} {
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
	    if {$has_funnel != 0} {
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
		if {$has_funnel == 0} {
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
			enableTraceEncoder $core
		}
    } elseif {$opt == "off"} {
		foreach core $coreList {
			disableTraceEncoder $core
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

    foreach baseAddress $traceBaseAddresses {
		set newval [expr [word [expr $baseAddress + $wp_control_offset]] | (1 << $bit)]
		mww [expr $baseAddress + $wp_control_offset] $newval
    }
}

proc wp_control_clear {core bit} {
    global wp_control_offset
    global traceBaseAddresses

    foreach baseAddress $traceBaseAddresses {
		set newval [expr [word [expr $baseAddress + $wp_control_offset]] & ~(1 << $bit)]
		mww [expr $baseAddress + $wp_control_offset] $newval
    }
}

proc wp_control_get_bit {core bit} {
    global wp_control_offset
    global traceBaseAddresses

    set baseAddress [lindex $traceBaseAddresses 0]
    return [expr ([word [expr $baseAddress + $wp_control_offset]] >> $bit) & 0x01]
}

proc wp_control {cores {bit ""} {val ""}} {
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

    return [expr ([word [expr $traceBaseAddrArray($core) + $xti_control_offset]] >> ($idx*4)) & 0xF]
}

proc xti_action_write {core idx val} {
    global xti_control_offset
    global traceBaseAddrArray

    set zeroed [expr ([word [expr $traceBaseAddrArray($core) + $xti_control_offset]] & ~(0xF << ($idx*4)))]
    set ored [expr ($zeroed | (($val & 0xF) << ($idx*4)))]
    mww [expr $traceBaseAddrArray($core) + $xti_control_offset] $ored
}

proc xto_event_read {core idx} {
    global xto_control_offset
    global traceBaseAddrArray
    return [expr ([word [expr $traceBaseAddrArray($core) + $xto_control_offset]] >> ($idx*4)) & 0xF]
}

proc xto_even_write {core idx val} {
    global xto_control_offset
    global traceBaseAddrArray

    set zeroed [expr ([word [expr $traceBaseAddrArray($core) + $xto_control_offset]] & ~(0xF << ($idx*4)))]
    set ored [expr ($zeroed | (($val & 0xF) << ($idx*4)))]
    mww [expr $traceBaseAddrArray($core) + $xto_control_offset] $ored
}

proc xti_action {cores {idx ""} {val ""}} {
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
    global traceFunnelAddress
    global traceBaseAddrArray
    global CABaseAddrArray
    global num_cores
    global has_funnel
	global have_htm

    # put all cores and funnel in a known state

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

    if {($traceFunnelAddress != 0x00000000) && ($traceFunnelAddress != "")} {
		set traceBaseAddrArray(funnel) $traceFunnelAddress
		set has_funnel 1
		setSink funnel "SRAM"
    } else {
		set has_funnel 0
    }

    set have_htm [checkHaveHTM]
	
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

proc dtr {{cores "all"}} {
    global traceBaseAddrArray
    global te_control_offset
    global te_impl_offset
    global te_sinkbase_offset
    global te_sinkbasehigh_offset
    global te_sinklimit_offset
    global te_sinkwp_offset
    global te_sinkrp_offset
    global te_sinkdata_offset

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
		cleartrace
		trace on
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
	cleartrace
	trace on
}

proc sample {} {
	global verbose
	set verbose 2
	stoponwrap 0 on
	setTeStallEnable 0 on
	tracemode 0 sample
	tracedst 0 sram
	cleartrace
	trace on
}

proc tsallon {} {
	ts all on
	tsbranch all all
	tsitc all on
	tsowner all on
}

init
tracedst
echo -n ""

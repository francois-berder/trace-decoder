-include ../makefile.init
-include ../version.mk

ifneq ($(MINGWLIBS),)
        pathsearch = $(firstword $(wildcard $(addsuffix $(1),$(LIBSEARCHPATH))))
        COPYFILES = $(foreach file,$(MINGWLIBS),$(call pathsearch,$(file)))
endif

RM := rm -rf

CFLAGS += -DDECODER_VERSION=\"$(TRACE_DECODER_VERSION)\" -O3 -Wall -Wformat=0
SWIGCFLAGS += -DDECODER_VERSION=\"$(TRACE_DECODER_VERSION)\" -O -Wall -Wformat=0

# All Target
Release release all: $(EXECUTABLE) $(SWIGLIB) $(SWTEXECUTABLE)

# Tool invocations
$(EXECUTABLE): $(OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	$(CROSSPREFIX)g++ -L$(LIBPATH) $(LNFLAGS) -o $(EXECUTABLE) $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

$(SWIGLIB): $(SWIGLIBOBJS)
	@echo 'Building swig library: $@'
	@echo 'Invoking: GCC C++ Linker'
	$(CROSSPREFIX)g++ -shared $(PICLIBFLAGS) -L$(LIBPATH) $(SWIGLIBOBJS) $(LIBS) -o $(SWIGLIB)
	@echo 'Finished building swig library: $@'
	@echo ' '

$(SWTEXECUTABLE): $(SWTOBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	$(CROSSPREFIX)g++ -L$(LIBPATH) $(LNFLAGS) -o $(SWTEXECUTABLE) $(SWTOBJS)
	@echo 'Finished building target: $@'
	@echo ' '

$(SWIGOBJ): $(SWIGCPP)
	@echo 'Building file $@'
	@echo 'Invoking: GCC C++ Compiler'
	$(CROSSPREFIX)g++ $(CFLAGS) $(SWIGCFLAGS) -c $< -I"$(LIBPATH)" -I"../include" $(JNIINCLUDEPATHS)
	@echo 'Finished building: $@'
	@echo ' '

$(SWIGCPP): $(INCLUDES) $(SWIGINCLUDES)
	@echo 'Building file $@'
	@echo 'Invoking: SWIG Compiler'
	swig -package com.sifive.trace -outdir com/sifive/trace -java -c++ -o dqr_wrap.cpp -I"$(LIBPATH)" -I../include ../include/dqr.i
	javac com/sifive/trace/*.java
	jar cf TraceDecoder.jar com/sifive/trace/*
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) *.d $(OBJS) $(EXECUTABLE) $(SWIGCPP) $(SWIGOBJ) $(SWIGLIB) $(SWTEXECUTABLE) $(SWTOBJS) com/sifive/trace/*.java com/sifive/trace/*.class TraceDecoder.jar
	-@echo ' '

install:
ifdef SWIGLIB
	mkdir -p "$(INSTALLPATH)/lib"
	cp -rp $(SWIGLIB) "$(INSTALLPATH)/lib"
	cp -rp TraceDecoder.jar "$(INSTALLPATH)/lib"
ifdef MINGWLIBS
	cp -p $(COPYFILES) "$(INSTALLPATH)/lib"
endif
endif
ifdef EXECUTABLE
	mkdir -p "$(INSTALLPATH)/bin"
	cp -rp $(EXECUTABLE) "$(INSTALLPATH)/bin"
endif

install-dev:
ifndef FS_DEV_TRACE_PLUGIN_FOLDER
	$(error FS_DEV_TRACE_PLUGIN_FOLDER is undefined)
endif
ifndef FS_DEV_TRACE_SCRIPT_FOLDER
	$(error FS_DEV_TRACE_SCRIPT_FOLDER is undefined)
endif
	cp TraceDecoder.jar $(FS_DEV_TRACE_PLUGIN_FOLDER)
	cp $(SWIGLIB) $(FS_DEV_TRACE_PLUGIN_FOLDER)/os/win32/x86_64
	cp ../scripts/trace.tcl $(FS_DEV_TRACE_SCRIPT_FOLDER)

.PHONY: all clean dependents install install-dev

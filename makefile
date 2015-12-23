
ifeq (,$(CPU_TARGET))
	CPU_TARGET=i686
endif

CC=gcc
CFLAGS=-c -g -DCPU$(CPU_TARGET)


SRC = bcc.c scope.c opstack.c function.c regalloc.c symtab.c token.c log.c \
	cpu/generic/cpu_generic.c cpu/$(CPU_TARGET)/cpu_$(CPU_TARGET).c
	
HDR = cpu.h scope.h opstack.h function.h regalloc.h symtab.h token.h cpu.h log.h \
	cpu/$(CPU_TARGET)/cpu_$(CPU_TARGET).h

LSRC	= $(notdir $(SRC))
OBJECTS = $(LSRC:%.c=%.o)

bcc: cpuspec.h $(OBJECTS)
	$(CC) -o bcc $(OBJECTS)

clean:	
	rm *.o cpuspec.h

cpuspec.h:	cpu/$(CPU_TARGET)/cpu_$(CPU_TARGET).h
	cp	$< $@
	
%.o:%.c
	$(CC) $(CFLAGS) -o $(notdir $@) $<

%.o:cpu/generic/%.c
	$(CC) $(CFLAGS) -o $(notdir $@) $<

%.o:cpu/$(CPU_TARGET)/%.c
	$(CC) $(CFLAGS) -o $(notdir $@) $<

bcc.o:  		bcc.c $(HDR)
scope.o:  		scope.c $(HDR)
opstack.o: 		opstack.c $(HDR)
function.o:		function.c $(HDR)
regalloc.o:		regalloc.c $(HDR)
symtab.o:	  	symtab.c $(HDR)
token.o:		token.c $(HDR)
log.o:  		log.c $(HDR)
cpu_generic.o:	cpu/generic/cpu_generic.c $(HDR)

cpu_$(CPU_TARGET).o:	cpu/$(CPU_TARGET)/cpu_$(CPU_TARGET).c $(HDR)


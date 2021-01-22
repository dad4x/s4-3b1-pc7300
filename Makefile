#
# Makefile for s4 programs.
#
# MUST be written in ancient Make syntax for use on the 3b1.
#

all:	compile lib exe

install:
	cp $(EXE) $(INSTALLDIR)	

INSTALLDIR = ~/bin


INC	= -I.

DEBUG   = -g -Wall 
OPTIM   = -O
CFLAGS  = $(OPTIM) $(DEBUG) $(INC)
CC      = gcc

S4LIB	= libs4.a

S4DATE 	 = s4date
S4DISK 	 = s4disk
S4DUMP 	 = s4dump
S4EXPORT = s4export
S4FSCK	 = s4fsck
S4FS	 = s4fs
S4IMPORT = s4import
S4MERGE  = s4merge
S4MKFS	 = s4mkfs
S4TEST	 = s4test
S4VOL 	 = s4vol

EXE	= $(S4DATE) $(S4DISK) $(S4DUMP) $(S4EXPORT) $(S4FS) $(S4FSCK)  \
	  $(S4IMPORT) $(S4MERGE) $(S4MKFS) $(S4TEST) $(S4VOL) 

LIBOPTS	= -L. -ls4

LIBOBJ	= s4d.o 

EXEOBJ	= s4date.o s4disk.o s4dump.o s4export.o s4fs.o s4fsck.o \
	  s4import.o s4merge.o s4mkfs.o s4test.o s4vol.o ismounted.o

OBJ	= $(LIBOBJ) $(EXEOBJ)

LIB	= $(S4LIB)

# special compile with FsTYPE definition
s4fsck.o : s4fsck.c
	    $(CC) $(CFLAGS) -c -DFsTYPE=2 $<

$(S4LIB):   $(LIBOBJ)
	    $(AR) rvu $(S4LIB) $(LIBOBJ)

$(S4DATE):  s4date.o $(LIB)
	    $(CC) s4date.o $(LIBOPTS) -o $@

$(S4DISK):  s4disk.o $(LIB)
	    $(CC) s4disk.o $(LIBOPTS) -o $@

$(S4DUMP):  s4dump.o $(LIB)
	    $(CC) s4dump.o $(LIBOPTS) -o $@

$(S4EXPORT):  s4export.o $(LIB)
	    $(CC) s4export.o $(LIBOPTS) -o $@

$(S4FS):    s4fs.o $(LIB)
	    $(CC) s4fs.o $(LIBOPTS) -o $@

$(S4FSCK):  s4fsck.o  ismounted.o $(LIB)
	    $(CC) s4fsck.o ismounted.o $(LIBOPTS) -o $@

$(S4IMPORT):  s4import.o $(LIB)
	    $(CC) s4import.o $(LIBOPTS) -o $@

$(S4MERGE): s4merge.o $(LIB)
	    $(CC) s4merge.o $(LIBOPTS) -o $@

$(S4MKFS):  s4mkfs.o $(LIB)
	    $(CC) s4mkfs.o $(LIBOPTS) -o $@

$(S4TEST):  s4test.o $(LIB)
	    $(CC) s4test.o $(LIBOPTS) -o $@

$(S4VOL):   s4vol.o $(LIB)
	    $(CC) s4vol.o $(LIBOPTS) -o $@


compile:    $(OBJ) $(LIB)

link:	    $(EXE)

lib:	    $(LIB)

exe:	    $(EXE)

clean:
	    rm -f $(LIB) $(EXE) $(OBJ)

# everything depends on s4d.h
$(OBJ)	    : s4d.h

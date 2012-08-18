
default: all

CC = gcc

CONFIG += ansi
CONFIG += debug

IFLAGS = -I..

CFLAGS += $(IFLAGS)

CxPath = ../cx
BinPath = ../bin
PfxBldPath = ../satsyn-bld
BldPath = $(PfxBldPath)/satsyn

ExeList = satsyn
Deps := $(ExeList)
ExeList := $(addprefix $(BinPath)/,$(ExeList))
Objs = $(addprefix $(BldPath)/,$(addsuffix .o,$(Deps)))

include $(CxPath)/include.mk

all: $(ExeList)

$(BinPath)/satsyn: $(BldPath)/satsyn.o \
	$(addprefix $(CxBldPath)/, bstree.o fileb.o ospc.o rbtree.o syscx.o)
	$(CC) $(CFLAGS) -o $@ $^

$(BldPath)/satsyn.o: dimacs.c pla.c promela.c \
	inst-sat3.c inst-matching.c inst-coloring.c inst-bit3.c


.PHONY: clean
clean:
	rm -fr $(PfxBldPath)
	rm -f $(ExeList)


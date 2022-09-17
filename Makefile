#
# Makefile for jsh
#

SRC = ./src
INC = -I./src
LIB = 

BIN = /usr/local/bin
ETC = /usr/local/etc
JSHDIR = $(ETC)/jsh.d
SAMPLE = group.jailed.conf

#CFLAGS := -O2 -Wall -Wno-unused-but-set-variable -g $(INC)
CFLAGS := -std=gnu99 -DDEBUG_JSH -O2 -Wall -Wno-unused-but-set-variable -g $(INC)
CC := gcc
AR := ar
RM := rm -rf
CP := cp -rf

default: jsh

OBJS =	$(SRC)/jsh.o $(SRC)/utils.o $(SRC)/mylex.o

jsh: $(OBJS)
	$(CC) -o $@ $^ /usr/local/lib/libocli.a -lpcre -lreadline

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

man.h:
	$(SRC)/grabman.py > $(SRC)/man.h

install:
	install -m 755 -o root -g root jsh $(BIN)
	install -m 644 -o root -g root -D conf/$(SAMPLE) $(JSHDIR)/$(SAMPLE)

clean:
	$(RM) jsh $(SRC)/*.o

#
# Makefile for jsh
#

SRC = ./src
INC = -I./src
LIB = 

#CFLAGS := -O2 -Wall -Wno-unused-but-set-variable -g $(INC)
CFLAGS := -std=gnu99 -DDEBUG_JSH -O2 -Wall -Wno-unused-but-set-variable -g $(INC)
CC := gcc
AR := ar
RM := rm -rf
CP := cp -rf

default: jsh

OBJS =	$(SRC)/jsh.o $(SRC)/utils.o $(SRC)/mylex.o

man.h:
	$(SRC)/grabman.py > $(SRC)/man.h

jsh: $(OBJS)
	$(CC) -o $@ $^ /usr/local/lib/libocli.a -lpcre -lreadline

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

install:
	install -m 755 -o root -g root jsh /usr/local/bin

clean:
	$(RM) jsh $(SRC)/*.o

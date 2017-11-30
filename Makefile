PROGNAME=shutdownd


CC = gcc
CFLAGS = -g -W -Wall -I.
LDLIBS =

SRC = cmdline.c $(PROGNAME).c

OBJ = $(SRC:%.c=%.o)

$(PROGNAME):$(OBJ)
	$(CC) -o $@ $(CFLAGS) $(OBJ) $(LDLIBS)

cmdline.c: cmdline.cli
	clig $<

mrproper: clean
	rm -f cmdline.c cmdline.h

clean:
	rm -f *.o *~ $(PROGNAME)

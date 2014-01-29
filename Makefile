DEBUG?= -g
CFLAGS?= -O2 -Wall -W
CCOPT= $(CFLAGS)
INCS=
LIBS?= -lz

OBJ= sisopen.o antigetopt.o
PRGNAME= sisopen

all: sisopen

antigetopt.o: antigetopt.c antigetopt.h
sisopen.o: sisopen.c langtab.h

sisopen: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ) $(LIBS)

nozlib:
	make COMPILE_TIME=-DNOZLIB LIBS=

.c.o:
	$(CC) -c $(CCOPT) $(DEBUG) $(COMPILE_TIME) $(INCS) $<

clean:
	rm -rf $(PRGNAME) *.o

dep:
	$(CC) -MM *.c

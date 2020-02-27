CC = gcc
CXX = g++

.PHONY: default all clean

default: prog
all: default

OBJECTS = main.o speed-test.o

prog: $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@

speed-test.o:  speed-test.c speed-test.h
	$(CC) -c -o speed-test.o speed-test.c

main.o: main.cpp speed-test.h
	$(CXX) -c -o main.o main.cpp

clean:
	-rm -f *.o prog

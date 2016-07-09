CC=gcc
CFLAGS=-g -O3 -Wall -DUSE_SIMD
LDFLAGS=-g

all: ssdv-cbec

ssdv-cbec: main.o ssdv.o rs8.o ssdv.h rs8.h
	$(CXX) $(LDFLAGS) main.o ssdv.o rs8.o -o ssdv-cbec -lcm256

.c.o:
	$(CXX) $(CFLAGS) -c $< -o $@ -lcm256

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 ssdv-cbec ${DESTDIR}/usr/bin

clean:
	rm -f *.o ssdv-cbec

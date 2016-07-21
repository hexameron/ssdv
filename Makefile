CC=gcc
CFLAGS=-g -O3 -Wall
LDFLAGS=-g

all: ssdv

ssdv:	main.o addblocks.o ssdv-cbec.o ssdv.o cbec.o rs8.o ssdv.h rs8.h
	$(CXX) $(LDFLAGS) cbec.o ssdv-cbec.o rs8.o -o ssdv-cbec -lcm256
	$(CXX) $(LDFLAGS) main.o ssdv.o rs8.o -o ssdv
	$(CXX) $(LDFLAGS) addblocks.o ssdv.o rs8.o -o addblocks -lcm256

.c.o:	$(CC) $(CFLAGS) -c $< -o $@
ssdv-cbec.o:	ssdv-cbec.cxx
	$(CXX) $(CFLAGS) -c ssdv-cbec.cxx -o $@
cbec.o:	cbec.cxx
	$(CXX) $(CFLAGS) -c cbec.cxx -o $@
addblocks.o:	addblocks.cxx
	$(CXX) $(CFLAGS) -c addblocks.cxx -o $@

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 addblocks ${DESTDIR}/usr/bin
	install -m 755 ssdv-cbec ${DESTDIR}/usr/bin
	install -m 755 ssdv ${DESTDIR}/usr/bin
clean:
	rm -f *.o ssdv-cbec ssdv addblocks

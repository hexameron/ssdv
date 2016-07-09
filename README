## SSDV - simple command line app for encoding / decoding SSDV data

Created by Philip Heron <phil@sanslogic.co.uk>
http://www.sanslogic.co.uk/ssdv/
Modified for CBEC by Richard Meadows <richardeoin>

Robust packetised data transfer using a Cauchy Block Erasure Code (CBEC),
Rate = 2/3.

Also uses the Reed-Solomon codec written by Phil Karn, KA9Q.

## ENCODING

$ ssdv -e -c TEST01 -i ID input.jpeg output.bin

This encodes the 'input.jpeg' image file into SSDV packets stored in
the 'output.bin' file. TEST01 (the callsign, an alphanumeric string up
to 6 characters) and ID (a number from 0-255) are encoded into the
header of each packet. The ID should be changed for each new image
transmitted to allow the decoder to identify when a new image begins.

The output file contains a series of SSDV packets, each packet always
being 256 bytes in length. Additional data may be transmitted between
each packet, the decoder will ignore this.

## DECODING

$ ssdv -d input.bin output.jpeg

This decodes a file 'input.bin' containing a series of SSDV packets
into the JPEG file 'output.jpeg'.

## LIMITATIONS

The input file size must be less than 4MB, but could be extended to
about 10MB using the same protocol.

## PACKET FORMAT

| Offset | Name | Size | Description
| --- | --- | --- | ---
| 0  | Sync Byte   | 1 | 0x55 - May be preceded by one or more sync bytes
| 1  | Packet Type | 1 | 0x68 - CBEC Normal mode (224 byte packet + 32 byte FEC)
|    |             |   | 0x69 - CBEC No-FEC mode (256 byte packet)
| 2  | Callsign    | 4 | Base-40 encoded callsign. Up to 6 digits
| 6  | Image ID    | 1 | Normally beginning at 0 and incremented by 1 for each new image
| 7  | Packet      | 2 | The packet number, beginning at 0 for each new image (big endian)
| 9  | Width       | 1 | Width of the image (pixels / 16) 0 = Invalid
| 10 | Height      | 1 | Height of the image (pixels / 16) 0 = Invalid
| 11 | Flags       | 1 | 00000e00: 00 = Reserved, e = EOI flag (1 = Last Packet)
| 12 | Sequences   | 1 | Number of CBEC sequences 0 = Invalid
| 13 | Blocks      | 1 | Number of Original Blocks per CBEC sequence
| 14 | Leftovers   | 1 | Number of unused bytes at the end of the last block of this sequence.
| 15 | Payload     | 205/237 | Payload data
| 220/252 | Checksum | 4 | 32-bit CRC
| 224 | FEC        | 32 | Reed-Solomon forward error correction data. Normal mode only (0x68)

## INSTALLING

First you need to install the `cm256` library

```
sudo apt-get install cmake
git clone https://github.com/richardeoin/cm256.git
cd cm256
mkdir build
cd build
cmake ..
make
sudo make install
sudo ldconfig
```

In this directory, edit the Makefile $CFLAGS `-D` to use the correct
section of gf256.h for you architecture. `-DUSE_SIMD` on intel,
`-DUSE_NEON` on ARMv7 (raspi v2,v3) or `-DNO_SIMD` otherwise.

then

```
make
```

## TODO

* Allow image width/height parameters to be specified from the command line.
* use cmake to set NO_SIMD/USE_NEON/USE_SIMD automagically

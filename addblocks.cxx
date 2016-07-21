/*                                                                       */
/* SSDV - Slow Scan Digital Video                                        */
/*=======================================================================*/
/* Copyright 2011-2016 Philip Heron <phil@sanslogic.co.uk>               */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/*
 * Backwards compatibility with SSDV: Packets need to have valid id numbers
 * and checksums. Parity blocks need to be passed to the server, without
 * corrupting local decodes.
 *
 * Bytes 0-10 are "known" headers and can be reconstructed from good packets.
 * Bytes 7 & 8 are big endian packet numbers - use -ve numbers for parity blocks.
 * Bytes 9 & 10 are width & height of image - set to zero for parity blocks.
 * Bytes 11 to 219 ( or 251 ) are data that needs Block FEC replacement.
 * CBEC block size is 240 bytes (only 208 are used for "normal" SSDV).
 * For simplicity, adding 2 parity blocks after every 8 data blocks.
 */

#define DATABLOCKS	( 8)
#define PARITYBLOCKS	( 4)
#define CBEC_SIZE_NOFEC	( 240)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "rs8.h"
#include "ssdv.h"
#include "cm256/cm256.h"

void exit_usage()
{
	fprintf(stderr,
		"Usage: addblocks [-e|-d] [<in file>] [<out file>]\n"
		"\n"
		"  -d Recover missing SSDV packets using parity blocks.\n"
		"  -t Drop a percentage of packets before attempting recovery.\n"
		"\n"
		"  -e Add parity blocks to a file of SSDV packets.\n"
		"  -l (1/8) fec rate is less\n"
		"  -n (2/8) fec rate is normal (default)\n"
		"  -m (3/8) fec rate is more\n"
		"  -g (4/8) fec rate is greatest\n"
		"  -t Drop a percentage of output packets.\n"
		"\n");
	exit(-1);
}

static void fixcrc(uint8_t *pkt, int nofec)
{
	uint32_t crc, x;
	uint8_t i, d;
	size_t length;

	if (nofec)
		length = 255 - 4;
	else
		length = 255 - 32 - 4;

	crc = 0xFFFFFFFF;
	for(d = 1; d <= length; d++) {
		x = (crc ^ pkt[d]) & 0xFF;
                for(i = 8; i > 0; i--) {
			if(x & 1)
				x = (x >> 1) ^ 0xEDB88320;
			else
				x = x >> 1;
		}
		crc = (crc >> 8) ^ x;
	}
	crc ^= 0xFFFFFFFF;
	pkt[d++] = (crc >> 24) & 255;  // checksum msb
	pkt[d++] = (crc >> 16) & 255;
	pkt[d++] = (crc >>  8) & 255;
	pkt[d++] = (crc >>  0) & 255;  // checksum lsb
}

int main(int argc, char *argv[])
{
	int c, i, j, k;
	FILE *fin = stdin;
	FILE *fout = stdout;
	char encode = -1;
	int droptest = 0;
	int errors = 0;
	int fec_rate = 2;
	uint8_t pkt_nofec;
	uint8_t *data = 0;
	uint8_t pkt[SSDV_PKT_SIZE];
	uint8_t pkt2[SSDV_PKT_SIZE];
	size_t  data_size;
	cm256_block_t  blockptr[8];
	ssdv_packet_info_t  info;
	cm256_encoder_params  params;

	params.OriginalCount = DATABLOCKS;
	params.RecoveryCount = PARITYBLOCKS;
	params.BlockBytes = CBEC_SIZE_NOFEC;

	opterr = 0;
	while((c = getopt(argc, argv, "deghlmnt")) != -1)
	{
		switch(c)
		{
		case 'e': encode = 1; break;
		case 'd': encode = 0; break;
		case 'l': fec_rate = 1; break;
		case 'n': fec_rate = 2; break;
		case 'm': fec_rate = 3; break;
		case 'g': fec_rate = 4; break;
		case 't': droptest = 10; break;
		default :
		case 'h':
		case '?': exit_usage();
		}
	}

	c = argc - optind;
	if(c > 2) exit_usage();

	for(i = 0; i < c; i++)
	{
		if(!strcmp(argv[optind + i], "-")) continue;

		switch(i)
		{
		case 0:
			fin = fopen(argv[optind + i], "rb");
			if(!fin)
			{
				fprintf(stderr,
                        "Error opening '%s' for input:\n", argv[optind + i]);
				perror("fopen");
				goto err_out;
			}
			break;

		case 1:
			fout = fopen(argv[optind + i], "wb");
			if(!fout)
			{
				fprintf(stderr,
                        "Error opening '%s' for output:\n", argv[optind + i]);
				perror("fopen");
				goto err_out;
			}
			break;
		}
	}

	if (cm256_init_(CM256_VERSION) < 0) {
		fprintf(stderr, "CM256 library failure !\n");
		goto err_out;
	}
	if (fread(pkt, 1, SSDV_PKT_SIZE, fin) < 1) {
		fprintf(stderr, "Unable to read file !\n");
		goto err_out;
	}

        if(droptest > 0)
                fprintf(stderr,"Drop test enabled: %d percent\n", droptest);

	data_size = DATABLOCKS * 1024;  // Allow 8K internal storage, use 16 * 256 bytes
	data = (uint8_t*)malloc(data_size);
	if (!data) {
		fprintf(stderr, "Unable to allocate memory !\n");
		goto err_out;
	}

	switch(encode)
	{
	case 0: /* Decode */
		uint16_t pnum, pbits, pbase, isparity, index;
		i = j = k = 0;
		isparity = index = 0;
		pbase = 65535 >> 3;
		blockptr[0].Index = DATABLOCKS;

		do {
			if (ssdv_dec_is_packet(pkt, &errors) != 0) {
				/* parity packets fail because of no width or height
						duplicate effects of "ssdv_dec_is_packet" */
				if( (decode_rs_8(&pkt[1], 0, 0, 0) < 0) && (pkt[1] != 0x67) )
					continue;
				uint8_t type = pkt[1] - 0x66;
				if (type > 1) continue;
				memcpy(pkt2, pkt, SSDV_PKT_SIZE);
				fixcrc(pkt, type);
				uint8_t crcbase = 220 + (type ? 32 : 0);
				if (pkt[crcbase+0] != pkt2[crcbase+0]) continue;
				if (pkt[crcbase+1] != pkt2[crcbase+1]) continue;
				if (pkt[crcbase+2] != pkt2[crcbase+2]) continue;
				if (pkt[crcbase+3] != pkt2[crcbase+3]) continue;
			}
			pkt_nofec = pkt[1] - 0x66;
			if(rand() < (RAND_MAX / 100) * droptest)
				continue;
			i++;
			pnum = (uint16_t)pkt[8] + ((uint16_t)pkt[7] << 8); // Packet id
			if (pkt[9] == 0 && pkt[10] == 0) {  // Parity block
				isparity = 1;
				pnum = 65535 - pnum;
				pbits = pnum & 3;
				pnum = (pnum - pbits) * 2 + pbits;
			} else {
				isparity = 0;
			}

			if ((pnum >> 3) != pbase) {
				/* output raw packets with no recovery */
				for (int n = 0; n <= j; n++) {
					if (blockptr[n].Index < DATABLOCKS) {
						fwrite(data + SSDV_PKT_SIZE * n, 1, SSDV_PKT_SIZE, fout);
						k++;
						index = (pbase << 3) + blockptr[n].Index;
					}
				}
				pbase = pnum >> 3;
				j = 0;
			} else
				j++;
			blockptr[j].Index = (pnum & 7) + (isparity ? DATABLOCKS : 0);
			blockptr[j].Block = data + SSDV_PKT_SIZE * (DATABLOCKS + j);
			memcpy(data + SSDV_PKT_SIZE * j, &pkt[0], SSDV_PKT_SIZE);
			memcpy(blockptr[j].Block, &pkt[11], CBEC_SIZE_NOFEC);

			if  (j > 6) {
				/* Recover a full block */
				j = 0;
				if (cm256_decode(params, blockptr)) {
					fprintf(stderr, "Recovery FAILED !\n" );
					continue;
				}
				for (int n = 0; n < DATABLOCKS; n++) {
					// fix recovered packets and sort by packet id
					uint8_t *target = data + SSDV_PKT_SIZE * blockptr[n].Index;
					void *source = blockptr[n].Block;
					memcpy(&target[11], source, CBEC_SIZE_NOFEC);

					index = (pbase << 3) + blockptr[n].Index;
					target[7] = index >> 8;  // image_id
					target[8] = index & 255; // image_id
					target[9] = data[9];  // width
					target[10] = data[10];  //height
					fixcrc( target, pkt_nofec );
					if (!pkt_nofec)
						encode_rs_8( &target[1], &target[224], 0);
				}
				for (int n = 0; n < DATABLOCKS; n++) {
					uint8_t *source = data + SSDV_PKT_SIZE * n;
					fwrite(source, 1, SSDV_PKT_SIZE, fout);
					k++;
				}
				blockptr[0].Index = DATABLOCKS;
			}

		} while(fread(pkt, 1, SSDV_PKT_SIZE, fin) > 0);

		/* output final packets with no recovery */
		for (int n = 0; n <= j; n++) {
			if (blockptr[n].Index < DATABLOCKS) {
				fwrite(data + SSDV_PKT_SIZE * n, 1, SSDV_PKT_SIZE, fout);
				k++;
				index = (pbase << 3) + blockptr[n].Index;
			}
		}

		fprintf(stderr, "Recovered %d packets of %d\n", k, index + 1);
		break;

	case 1: /* Encode */
		if (ssdv_dec_is_packet(pkt, &errors) != 0) {
			fprintf(stderr, "Not an SSDV file !\n");
			break;
		}
		ssdv_dec_header(&info, pkt);
		fprintf(stderr, "SSDV callsign = %s\n", info.callsign_s);
		fprintf(stderr, "Image size = %d x %d\n", info.width, info.height);
		fprintf(stderr,"Adding FEC parity blocks at rate: %d/8\n", fec_rate);

		pkt_nofec = pkt[1] - 0x66;
		if (pkt_nofec > 1) {
			fprintf(stderr, "Mode not supported.\n");
                        break;
		}

		i = 0;
		k = 65535;  // USHRT_MAX;
		do {
			if(ssdv_dec_is_packet(pkt, &errors) != 0)
				continue;
			if(rand() >= (RAND_MAX / 100) * droptest)
				fwrite(pkt, 1, SSDV_PKT_SIZE, fout);

			j = i++ & 7;
			blockptr[j].Block = data + SSDV_PKT_SIZE * j;
			memcpy(blockptr[j].Block, &pkt[11], CBEC_SIZE_NOFEC);

			if (7 == j) {
				// add parity blocks
				uint8_t *target = data + SSDV_PKT_SIZE * DATABLOCKS;
				cm256_encode(
					params,         // Encoder parameters
					blockptr,       // Array of pointers to original blocks
					target);	// Pointer to array of parity blocks

				for (j = 0; j < fec_rate; j++) {
					pkt[7] = k >> 8;  // image_id
					pkt[8] = k & 255; // image_id
					pkt[9] = 0;  //width
					pkt[10] = 0; //height
					memcpy(&pkt[11], target + j * CBEC_SIZE_NOFEC,
									CBEC_SIZE_NOFEC);
					fixcrc(pkt, pkt_nofec);
					if (!pkt_nofec)
						encode_rs_8(&pkt[1], &pkt[224], 0);
					if(rand() >= (RAND_MAX / 100) * droptest)
						fwrite(pkt, 1, SSDV_PKT_SIZE, fout);
					k--;
				}	k = k - 4 + fec_rate;
			}
		} while(fread(pkt, 1, SSDV_PKT_SIZE, fin) > 0);

		fprintf(stderr, "Packets in:%d, out:%d more.\n", i, (i>>3) * fec_rate);
		break;

	default:
		fprintf(stderr, "No mode specified.\n");
		break;
	}

err_out:
	if(data) free(data);
	if(fin != stdin) fclose(fin);
	if(fout != stdout) fclose(fout);

	return(0);
}

/*                                                                       */
/* SSDV - Slow Scan Digital Video                                        */
/*=======================================================================*/
/* Copyright 2011-2016 Philip Heron <phil@sanslogic.co.uk>               */
/* Modified for BPG Copyright 2016 Richard Meadows <>                    */
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ssdv-cbec.h"

void exit_usage()
{
	fprintf(stderr,
		"Usage: ssdv-cbec [-e|-d] [-n] [-t <percentage>] [-c <callsign>]"
            " [-i <id>] [<in file>] [<out file>]\n"
		"\n"
		"  -e Encode data to SSDV packets.\n"
		"  -d Decode SSDV packets to data.\n"
		"\n"
		"  -n Encode packets with no FEC.\n"
		"  -b Use original SSDV packet types (0x66 and 0x67) for "
            "backwards compatibility.\n"
		"  -t For testing, drops the specified percentage of packets while "
            "decoding.\n"
		"  -c Set the callign. Accepts A-Z 0-9 and space, up to 6 characters.\n"
		"  -i Set the image ID (0-255).\n"
		"  -v Print data for each packet decoded.\n"
		"\n");
	exit(-1);
}

int main(int argc, char *argv[])
{
	int c, i;
	FILE *fin = stdin;
	FILE *fout = stdout;
	char encode = -1;
	char type = SSDV_TYPE_CBEC;
    char use_oldtype = 0;
	int droptest = 0;
	int verbose = 0;
	int errors;
	char callsign[7];
	uint8_t image_id = 0;
	ssdv_t ssdv;
    size_t r;

	uint8_t pkt[SSDV_PKT_SIZE], *data;
	size_t data_max_length, data_size;

	callsign[0] = '\0';

	opterr = 0;
	while((c = getopt(argc, argv, "ednbc:i:t:v")) != -1)
	{
		switch(c)
		{
		case 'e': encode = 1; break;
		case 'd': encode = 0; break;
        case 'n': type = SSDV_TYPE_CBEC_NOFEC; break;
        case 'b': use_oldtype = 1; break;
		case 'c':
			if(strlen(optarg) > 6)
				fprintf(stderr,
                        "Warning: callsign is longer than 6 characters.\n");
			strncpy(callsign, optarg, 7);
			break;
		case 'i': image_id = atoi(optarg); break;
		case 't': droptest = atoi(optarg); break;
		case 'v': verbose = 1; break;
		case '?': exit_usage();
		}
	}

	c = argc - optind;
	if(c > 2) exit_usage();

    /* Use old type for compatibility */
    if (use_oldtype) {
      switch(type) {
        case SSDV_TYPE_CBEC:       type = SSDV_TYPE_OLD; break;
        case SSDV_TYPE_CBEC_NOFEC: type = SSDV_TYPE_OLD_NOFEC; break;
      }
    }

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
				return(-1);
			}
			break;

		case 1:
			fout = fopen(argv[optind + i], "wb");
			if(!fout)
			{
				fprintf(stderr,
                        "Error opening '%s' for output:\n", argv[optind + i]);
				perror("fopen");
				return(-1);
			}
			break;
		}
	}

	switch(encode)
	{
	case 0: /* Decode */
        if(droptest > 0) {
            fprintf(stderr,"*** NOTE: Drop test enabled: %i ***\n", droptest);
        }

		ssdv_dec_init(&ssdv);

		data_size = 1024*1024*7; /* needs 7MB internal storage */
		data = (uint8_t*)malloc(data_size);
		ssdv_dec_set_buffer(&ssdv, data, data_size);

		i = 0;
		while(fread(pkt, 1, SSDV_PKT_SIZE, fin) > 0)
		{
			/* Drop % of packets */
			if(droptest && (rand() / (RAND_MAX / 100) < droptest)) continue;

			/* Test the packet is valid */
			if(ssdv_dec_is_packet(pkt, &errors) != 0) continue;

			if(verbose)
			{
				ssdv_packet_info_t p;

				ssdv_dec_header(&p, pkt);
				fprintf(stderr, "Decoded image packet. Callsign: %s,"
                        " Image ID: %d, "
                        " Packet ID: %d (%d errors corrected)\n"
                        ">> Type: %d, EOI: %d\n",
					p.callsign_s,
					p.image_id,
					p.packet_id,
					errors,
					p.type,
					p.eoi
				);
			}

			/* Feed it to the decoder */
			ssdv_dec_feed(&ssdv, pkt);
			i++;
		}

		fprintf(stderr, "Read %i packets\n", i);

        /* Run recovery operations */
		c = ssdv_dec_recover_data(&ssdv);

        /* recovery operation may fail */
        /* but we should still output what we have */

        /* Write out received data */
        uint8_t* out_ptr;
        uint8_t out_len;
        i = 0;
        while (ssdv_dec_get_data(&ssdv, &out_ptr, &out_len) == SSDV_OK)
        {
            fwrite(out_ptr, 1, out_len, fout);
            i++;
        }

        /* clean up */
		free(data);

		fprintf(stderr, "Wrote %i data blocks\n", i);

		break;

	case 1: /* Encode */
		ssdv_enc_init(&ssdv, type, callsign, image_id);

        /* grab some memory to use */
        data_max_length = 1024*1024*4; /* 4MB of data maximum */
		data_size       = 1024*1024*7; /* need 7MB internal storage */
		data = (uint8_t*)malloc(data_size);

        /* read in file */
        r = fread(data, 1, data_max_length, fin);

        if(ferror(fin)) {
            fprintf(stderr, "Error reading file\n");
            break;
        }
        if(!feof(fin)) {
            fprintf(stderr, "File too big to read\n");
            break;
        }

        /* setup this memory buffer - CREATE RECOVERY BLOCKS HERE*/
		ssdv_enc_set_buffer(&ssdv, data, r);

        fprintf(stderr, "Using %d CBEC sequences\n", ssdv.sequences);
        fprintf(stderr, "%d original blocks, %d recovery blocks each\n",
                ssdv.params.OriginalCount, ssdv.params.RecoveryCount);

        i = 0;
		while(1)
		{
			/* yield a packet */
            c = ssdv_enc_get_packet(&ssdv, pkt);

            if(c == SSDV_EOI)
            {
                fprintf(stderr, "ssdv_enc_get_packet said EOI\n");
                break;
            }
			if(c != SSDV_OK)
			{
				fprintf(stderr, "ssdv_enc_get_packet failed: %i\n", c);
				return(-1);
			}

            /* write yielded packet out */
			fwrite(pkt, 1, SSDV_PKT_SIZE, fout);
			i++;
		}

        /* clean up */
        ssdv_enc_done(&ssdv);
		free(data);

		fprintf(stderr, "Wrote %i packets\n", i);

		break;

    default:
		fprintf(stderr, "No mode specified.\n");
		break;
	}

	if(fin != stdin) fclose(fin);
	if(fout != stdout) fclose(fout);

	return(0);
}

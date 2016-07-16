/*                                                                       */
/* SSDV - Slow Scan Digital Video                                        */
/*=======================================================================*/
/* Copyright 2011-2016 Philip Heron <phil@sanslogic.co.uk>               */
/* Modified for CBEC Copyright 2016 Richard Meadows <>                   */
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
#include <string.h>
#include "ssdv.h"
#include "rs8.h"
#include "cm256/cm256.h"

static uint32_t crc32(uint8_t *data, size_t length)
{
	uint32_t crc, x;
	uint8_t i, *d;

	for(d = data, crc = 0xFFFFFFFF; length; length--)
	{
		x = (crc ^ *(d++)) & 0xFF;
		for(i = 8; i > 0; i--)
		{
			if(x & 1) x = (x >> 1) ^ 0xEDB88320;
			else x >>= 1;
		}
		crc = (crc >> 8) ^ x;
	}

	return(crc ^ 0xFFFFFFFF);
}

static uint32_t encode_callsign(char *callsign)
{
	uint32_t x;
	char *c;

	/* Point c at the end of the callsign, maximum of 6 characters */
	for(x = 0, c = callsign; x < SSDV_MAX_CALLSIGN && *c; x++, c++);

	/* Encode it backwards */
	x = 0;
	for(c--; c >= callsign; c--)
	{
		x *= 40;
		if(*c >= 'A' && *c <= 'Z') x += *c - 'A' + 14;
		else if(*c >= 'a' && *c <= 'z') x += *c - 'a' + 14;
		else if(*c >= '0' && *c <= '9') x += *c - '0' + 1;
	}

	return(x);
}

static char *decode_callsign(char *callsign, uint32_t code)
{
	char *c, s;

	*callsign = '\0';

	/* Is callsign valid? */
	if(code > 0xF423FFFF) return(callsign);

	for(c = callsign; code; c++)
	{
		s = code % 40;
		if(s == 0) *c = '-';
		else if(s < 11) *c = '0' + s - 1;
		else if(s < 14) *c = '-';
		else *c = 'A' + s - 14;
		code /= 40;
	}
	*c = '\0';

	return(callsign);
}

/*****************************************************************************/

static void ssdv_set_packet_conf(ssdv_t *s)
{
	/* Configure the payload size and CRC position */
	switch(s->type)
	{
        case SSDV_TYPE_OLD:
        case SSDV_TYPE_CBEC:
            s->pkt_size_payload = SSDV_PKT_SIZE - SSDV_PKT_SIZE_HEADER -
                SSDV_PKT_SIZE_CRC - SSDV_PKT_SIZE_RSCODES;
            s->pkt_size_crcdata = SSDV_PKT_SIZE_HEADER + s->pkt_size_payload - 1;
            break;

        case SSDV_TYPE_OLD_NOFEC:
        case SSDV_TYPE_CBEC_NOFEC:
            s->pkt_size_payload = SSDV_PKT_SIZE - SSDV_PKT_SIZE_HEADER -
                SSDV_PKT_SIZE_CRC;
            s->pkt_size_crcdata = SSDV_PKT_SIZE_HEADER + s->pkt_size_payload - 1;
            break;
	}
}

/* Integer division ceiling function for x > 0, y > 0 */
#define DIV_CEIL(x,y) (1 + ((x - 1) / y))

/**
 * Sets parameters to use for cm256 library
 */
static char ssdv_set_cm256_params(ssdv_t *s)
{
    /* cm256 params for CR = 2/3 */
    s->params.BlockBytes = s->pkt_size_payload;
    s->params.OriginalCount = s->blocks;
    s->params.RecoveryCount = DIV_CEIL(s->blocks, 2);

	return(SSDV_OK);
}

/**
 * Calculate and set CBEC parameters for a given data length
 */
static char ssdv_set_cbec_parameters(ssdv_t *s, size_t length)
{
    uint8_t seq = 1;
    uint16_t blocks = DIV_CEIL(length, s->pkt_size_payload);

    /* Find the number of sequences we need to contain all this data */
    /* At CR = 2/3 we can have at most 170 blocks in a GF(256) CBEC  */
    while(DIV_CEIL(blocks, seq) > 170) { seq++; }

    /* set params */
    s->sequences = seq;
    s->blocks = DIV_CEIL(blocks, seq);

    /* use these to set the cm256 parameters */
	return ssdv_set_cm256_params(s);
}

/*****************************************************************************/

static void ssdv_memset_prng(uint8_t *s, size_t n)
{
    /* A very simple PRNG for noise whitening */
    uint8_t l = 0x00;
    for(; n > 0; n--) *(s++) = (l = l * 245 + 45);
}

char ssdv_enc_init(ssdv_t *s, uint8_t type, char *callsign, uint8_t image_id)
{
    if (cm256_init())
    {
        return SSDV_ERROR;
    }

	memset(s, 0, sizeof(ssdv_t));
	s->image_id = image_id;
	s->callsign = encode_callsign(callsign);
	s->type = type;
	ssdv_set_packet_conf(s);

	return(SSDV_OK);
}

char ssdv_enc_set_buffer(ssdv_t *s, uint8_t *buffer, size_t length)
{
	s->out     = buffer;
	s->outp    = buffer;

    /* set parameters */
    if (ssdv_set_cbec_parameters(s, length) != SSDV_OK) {
        return (SSDV_ERROR);
    }

    /* allocate memory for recovery blocks */
    s->recovery_blocks = (uint8_t*)malloc(s->sequences *
                                          s->params.RecoveryCount *
                                          s->pkt_size_payload);

    /* set leftover bytes to noise */
    size_t bytes          = s->pkt_size_payload * s->blocks * s->sequences;
    size_t bytes_leftover = bytes - length;
    ssdv_memset_prng(s->out + length, bytes_leftover);

    /* allocate leftovers */
    uint8_t leftover;
    int16_t seq;
    for (seq = s->sequences-1; seq >= 0; seq--) {
        leftover = (bytes_leftover > s->pkt_size_payload) ?
            s->pkt_size_payload : bytes_leftover;
        s->leftovers[seq] = leftover;
        bytes_leftover -= leftover;
    }

    /* generate recovery blocks for each sequence */
    cm256_block blocks[256];
    uint8_t* recovery_ptr;
    uint8_t i;

    for (seq = 0; seq < s->sequences; seq++) {
        /* original blocks */
        for (i = 0; i < s->params.OriginalCount; i++)
        {
            blocks[i].Block = s->out +
                (s->pkt_size_payload*seq) +
                (s->sequences*s->pkt_size_payload*i);
        }

        /**
         * generate recovery blocks, starting from (s->out + bytes).
         *
         * while the original blocks are already interleaved, these
         * recovery blocks are not and will need to be interleaved by
         * ssdv_enc_get_packet.
         */
        recovery_ptr = s->recovery_blocks +
            (s->params.RecoveryCount*s->pkt_size_payload*seq);

        if (cm256_encode(s->params, blocks, recovery_ptr))
        {
            /* fail */
            return(SSDV_ERROR);
        }
    }

	return(SSDV_OK);
}

char ssdv_enc_get_packet(ssdv_t *s, uint8_t *pkt)
{
    uint8_t* outp;
    char r = SSDV_OK;
    uint32_t x;

    /* return if we're already reached the end of the image */
    if (s->blk >= (s->blocks + s->params.RecoveryCount)) {
        return(SSDV_EOI);
    }

    /* next data packet to read  */
    if (s->blk < s->blocks) {   /* original blocks, output sequentially */
        outp = s->out +
            ((s->blk*s->sequences) + s->seq) * s->pkt_size_payload;
    } else {                    /* recovery blocks, interleave */
        outp = s->recovery_blocks +
            ((s->params.RecoveryCount*s->seq) +
             (s->blk - s->blocks)) * s->pkt_size_payload;
    }

    /* check if we've reached it now */
    if ((s->blk >= (s->blocks + s->params.RecoveryCount)) &&
        (s->seq == (s->sequences-1))) {
        r = SSDV_EOI;
    }

    /* create the headers */
    pkt[0]   = 0x55;                /* Sync */
    pkt[1]   = 0x66 + s->type;      /* Type */
    pkt[2]   = s->callsign >> 24;
    pkt[3]   = s->callsign >> 16;
    pkt[4]   = s->callsign >> 8;
    pkt[5]   = s->callsign;
    pkt[6]   = s->image_id;         /* Image ID */
    pkt[7]   = s->packet_id >> 8;   /* Packet ID MSB */
    pkt[8]   = s->packet_id & 0xFF; /* Packet ID LSB */
    pkt[9]   = s->sequences;        /* Number of CBEC sequences */
    pkt[10]  = s->blocks; /* Number of Originial Blocks per CBEC sequence */
    pkt[11]  = 0b01000000;
    pkt[11] |= (r == SSDV_EOI ? 1 : 0) << 2; /* EOI flag (1 bit) */
    pkt[12]  = s->leftovers[s->seq];
    pkt[13]  = 0xFF;            /* compatibility */
    pkt[14]  = 0xFF;            /* compatibility */

    /* populate the payload */
    memcpy(&pkt[SSDV_PKT_SIZE_HEADER], outp, s->pkt_size_payload);

    /* Calculate the CRC codes */
    x = crc32(&pkt[1], s->pkt_size_crcdata);

    uint8_t i = 1 + s->pkt_size_crcdata;
    pkt[i++] = (x >> 24) & 0xFF;
    pkt[i++] = (x >> 16) & 0xFF;
    pkt[i++] = (x >> 8) & 0xFF;
    pkt[i++] = x & 0xFF;

    /* Generate the RS codes */
    if((s->type == SSDV_TYPE_OLD) ||
       (s->type == SSDV_TYPE_CBEC)) {

        encode_rs_8(&pkt[1], &pkt[i], 0);
    }

    /* Increment packet ID */
    s->packet_id++;

    /* rotate to next sequence */
    s->seq++;
    if (s->seq >= s->sequences) {
        /* start next block */
        s->seq = 0;
        s->blk++;
    }

    return(SSDV_OK);
}

char ssdv_enc_done(ssdv_t *s)
{
    free(s->recovery_blocks);

	return(SSDV_OK);
}


/*****************************************************************************/

char ssdv_dec_init(ssdv_t *s)
{
    if (cm256_init())
    {
        return SSDV_ERROR;
    }

	memset(s, 0, sizeof(ssdv_t));

	return(SSDV_OK);
}

char ssdv_dec_set_buffer(ssdv_t *s, uint8_t *buffer, size_t length)
{
	s->out  = buffer;
	s->outp = buffer;

	return(SSDV_OK);
}

/**
 * Feed in ssdv packets for decode
 *
 * Header is parsed and cbec_decode matrix annotated
 */
char ssdv_dec_feed(ssdv_t *s, uint8_t *packet)
{
	uint8_t seq, index, count;
	uint16_t packet_id;

	/* Read the packet header */
	packet_id            = (packet[7] << 8) | packet[8];

	/* If this is the first packet, write info */
	if(s->packet_id == 0)
	{
		char callsign[SSDV_MAX_CALLSIGN + 1];

		/* Read the fixed headers from the packet */
		s->type      = packet[1] - 0x66;
		s->callsign  = (packet[2] << 24) | (packet[3] << 16) |
            (packet[4] << 8) | packet[5];
		s->image_id  = packet[6];
        s->sequences = packet[9];
        s->blocks    = packet[10];

		/* Configure the payload size and CRC position */
		ssdv_set_packet_conf(s);

        /* Configure the CBEC parameters */
        ssdv_set_cm256_params(s);

		/* Display information about the image */
		fprintf(stderr, "Callsign: %s\n",
                decode_callsign(callsign, s->callsign));
		fprintf(stderr, "Image ID: %02X\n", s->image_id);
		fprintf(stderr, "Sequences: %i\n", s->sequences);
		fprintf(stderr, "Blocks: %i\n",    s->blocks);
	}

    /* Convert packet ID to sequence and block index */
    seq   = packet_id % s->sequences;
    index = packet_id / s->sequences;

    /* record leftovers */
    s->leftovers[seq] = packet[12];

    /* Fill in this packet in the CBEC matrix */
    count = s->cbec_blocks_counts[seq];
    s->cbec_matrix[(seq*256)+count].Block = s->outp;
    s->cbec_matrix[(seq*256)+count].Index = index;
    s->cbec_blocks_counts[seq]++;

    /* Copy data to buffer */
    memcpy(s->outp, &packet[SSDV_PKT_SIZE_HEADER], s->pkt_size_payload);
    s->outp += s->pkt_size_payload;

	return(SSDV_FEED_ME);
}

/**
 * Runs recovery operations on fed-in data
 */
char ssdv_dec_recover_data(ssdv_t *s)
{
    uint8_t seq;

    for (seq = 0; seq < s->sequences; seq++) {

        /* attempt to recover */
        if (cm256_decode(s->params, &s->cbec_matrix[(seq*256)])) {
            return(SSDV_ERROR);
        }
    }

	return(SSDV_OK);
}

/**
 * Returns each decoded packet in order
 */
char ssdv_dec_get_data(ssdv_t *s, uint8_t** data, uint8_t* length)
{
    uint16_t i = 0;

    while (1) {

        if (s->blk >= s->blocks) {
            return(SSDV_EOI);       /* done */
        }

        /* find block */
        /* TODO: less brute-forcey-method */
        for (i = 0; (i < 256) &&
                 (s->cbec_matrix[(s->seq*256)+i].Index != s->blk); i++);

        if (i < 256) {              /* found it */
            /* return pointer to block */
            *data = (uint8_t*)s->cbec_matrix[(s->seq*256)+i].Block;
        } else {                    /* block not found */
            *data = NULL;
        }

        /* for the final block length is minus leftovers */
        if (s->blk == (s->blocks-1)) {
            *length = s->pkt_size_payload - s->leftovers[s->seq];
        } else {
            *length = s->pkt_size_payload;
        }

        /* rotate to next sequence */
        s->seq++;
        if (s->seq >= s->sequences) {
            /* start next block */
            s->seq = 0;
            s->blk++;
        }


        if (*data == NULL) {        /* uhh, no block here */
            /* maybe a partial, continue to next  */
            continue;
        }

        return(SSDV_OK);
    }
}

char ssdv_dec_is_packet(uint8_t *packet, int *errors)
{
	uint8_t pkt[SSDV_PKT_SIZE];
	uint8_t type;
	uint16_t pkt_size_payload;
	uint16_t pkt_size_crcdata;
	ssdv_packet_info_t p;
	uint32_t x;
	int i;

	/* Testing is destructive, work on a copy */
	memcpy(pkt, packet, SSDV_PKT_SIZE);
	pkt[0] = 0x55;

	type = SSDV_TYPE_INVALID;

	if((pkt[1] == 0x66 + SSDV_TYPE_CBEC_NOFEC) ||
       (pkt[1] == 0x66 + SSDV_TYPE_OLD_NOFEC))
	{
		/* Test for a valid NOFEC packet */
		pkt_size_payload = SSDV_PKT_SIZE - SSDV_PKT_SIZE_HEADER -
            SSDV_PKT_SIZE_CRC;
		pkt_size_crcdata = SSDV_PKT_SIZE_HEADER + pkt_size_payload - 1;

		/* No FEC scan */
		if(errors) *errors = 0;

		/* Test the checksum */
		x = crc32(&pkt[1], pkt_size_crcdata);

		i = 1 + pkt_size_crcdata;
		if(x == ((pkt[i + 3]      ) | (pkt[i + 2] <<  8) |
                 (pkt[i + 1] << 16) | (pkt[i]     << 24)))
		{
			/* Valid, set the type and continue */
			type = SSDV_TYPE_CBEC_NOFEC;
		}
	}
	else if((pkt[1] == 0x66 + SSDV_TYPE_CBEC) ||
            (pkt[1] == 0x66 + SSDV_TYPE_OLD))
	{
		/* Test for a valid NORMAL packet */
		pkt_size_payload = SSDV_PKT_SIZE - SSDV_PKT_SIZE_HEADER -
            SSDV_PKT_SIZE_CRC - SSDV_PKT_SIZE_RSCODES;
		pkt_size_crcdata = SSDV_PKT_SIZE_HEADER + pkt_size_payload - 1;

		/* No FEC scan */
		if(errors) *errors = 0;

		/* Test the checksum */
		x = crc32(&pkt[1], pkt_size_crcdata);

		i = 1 + pkt_size_crcdata;
		if(x == ((pkt[i + 3]      ) | (pkt[i + 2] <<  8) |
                 (pkt[i + 1] << 16) | (pkt[i]     << 24)))
		{
			/* Valid, set the type and continue */
			type = SSDV_TYPE_CBEC;
		}
	}

	if(type == SSDV_TYPE_INVALID)
	{
		/* Test for a valid NORMAL packet with correctable errors */
		pkt_size_payload = SSDV_PKT_SIZE - SSDV_PKT_SIZE_HEADER -
            SSDV_PKT_SIZE_CRC - SSDV_PKT_SIZE_RSCODES;
		pkt_size_crcdata = SSDV_PKT_SIZE_HEADER + pkt_size_payload - 1;

		/* Run the reed-solomon decoder */
		pkt[1] = 0x66 + SSDV_TYPE_CBEC;
		i = decode_rs_8(&pkt[1], 0, 0, 0);

        if (i < 0) { /* Reed-solomon decoder failed */
            /* Maybe it had the old type? */

            /* Run the reed-solomon decoder */
            pkt[1] = 0x66 + SSDV_TYPE_OLD;
            i = decode_rs_8(&pkt[1], 0, 0, 0);

            if(i < 0) return(-1); /* Reed-solomon decoder failed */
        }

        /* Record errors */
        if(errors) *errors = i;

        /* Test the checksum */
        x = crc32(&pkt[1], pkt_size_crcdata);

        i = 1 + pkt_size_crcdata;
        if(x == ((pkt[i + 3]      ) | (pkt[i + 2] <<  8) |
                 (pkt[i + 1] << 16) | (pkt[i]     << 24)))
        {
            /* Valid, set the type and continue */
            type = SSDV_TYPE_CBEC;
        }
    }

    if(type == SSDV_TYPE_INVALID)
    {
        /* All attempts to read the packet have failed */
        return(-1);
    }

    /* Sanity checks */
    ssdv_dec_header(&p, pkt);

    if(p.type != type) return(-1);
    if(p.sequences == 0 || p.blocks == 0) return(-1);

    /* Appears to be a valid packet! Copy it back */
    memcpy(packet, pkt, SSDV_PKT_SIZE);

    return(0);
}

void ssdv_dec_header(ssdv_packet_info_t *info, uint8_t *packet)
{
    info->type       = packet[1] - 0x66;
    info->callsign   = (packet[2] << 24) | (packet[3] << 16) |
        (packet[4] << 8) | packet[5];
    decode_callsign(info->callsign_s, info->callsign);
    info->image_id   = packet[6];
    info->packet_id  = (packet[7] << 8) | packet[8];
    info->sequences  = packet[9];
    info->blocks     = packet[10];
    info->eoi        = (packet[11] >> 2) & 1;
}

/*****************************************************************************/

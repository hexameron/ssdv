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

#include <stdint.h>
#include "cm256/cm256.h"

#ifndef INC_SSDV_H
#define INC_SSDV_H
#ifdef __cplusplus
extern "C" {
#endif

#define SSDV_ERROR       (-1)
#define SSDV_OK          (0)
#define SSDV_FEED_ME     (1)
#define SSDV_HAVE_PACKET (2)
#define SSDV_BUFFER_FULL (3)
#define SSDV_EOI         (4)
#define SSDV_PARTIAL     (5)

/* Packet details */
#define SSDV_PKT_SIZE         (0x100)
#define SSDV_PKT_SIZE_HEADER  (0x0F)
#define SSDV_PKT_SIZE_CRC     (0x04)
#define SSDV_PKT_SIZE_RSCODES (0x20)

#define SSDV_MAX_CALLSIGN (6) /* Maximum number of characters in a callsign */

#define SSDV_TYPE_INVALID    (0xFF)
#define SSDV_TYPE_OLD        (0x00)
#define SSDV_TYPE_OLD_NOFEC  (0x01)
#define SSDV_TYPE_CBEC       (0x02)
#define SSDV_TYPE_CBEC_NOFEC (0x03)

typedef struct
{
	/* Packet type configuration */
    uint8_t type; /* 2 = Normal CBEC mode (224 byte packet + 32 bytes FEC),
                     3 = No-FEC CBEC mode (256 byte packet) */
	uint16_t pkt_size_payload;
	uint16_t pkt_size_crcdata;

	/* Image information */
	uint32_t callsign;
	uint8_t  image_id;
	uint16_t packet_id;

    /* CBEC parameters */
    cm256_encoder_params params;
    uint8_t sequences;          /* number of sequences in image, 0 = invalid */
    uint8_t blocks;             /* number of blocks to recover each sequence */
    uint8_t leftovers[256];     /* leftover bytes in final block of each seq */

	/* Output buffer */
	uint8_t *out;      /* Pointer to the beginning of the output buffer */
	uint8_t *outp;     /* Pointer to the next output byte               */

    /* Pointer to recovery blocks, used for encode */
    uint8_t* recovery_blocks;

    /* CBEC matrix used for decode */
    cm256_block cbec_matrix[256*256]; /* a matrix containing received packets */
    uint8_t cbec_blocks_counts[256];  /* count of blocks received for each seq*/
    uint8_t seq, blk;

} ssdv_t;

typedef struct {
	uint8_t  type;
	uint32_t callsign;
	char     callsign_s[SSDV_MAX_CALLSIGN + 1];
	uint8_t  image_id;
	uint16_t packet_id;
	uint8_t  eoi;
    uint8_t  sequences;
    uint8_t  blocks;
} ssdv_packet_info_t;

/* Encoding */
extern char ssdv_enc_init(ssdv_t *s, uint8_t type,
                          char *callsign, uint8_t image_id);
extern char ssdv_enc_set_buffer(ssdv_t *s, uint8_t *buffer, size_t length);
extern char ssdv_enc_get_packet(ssdv_t *s, uint8_t *pkt);
extern char ssdv_enc_done(ssdv_t *s);

/* Decoding */
extern char ssdv_dec_init(ssdv_t *s);
extern char ssdv_dec_set_buffer(ssdv_t *s, uint8_t *buffer, size_t length);
extern char ssdv_dec_feed(ssdv_t *s, uint8_t *packet);
extern char ssdv_dec_recover_data(ssdv_t *s);
extern char ssdv_dec_get_data(ssdv_t *s, uint8_t** data, uint8_t* length);

extern char ssdv_dec_is_packet(uint8_t *packet, int *errors);
extern void ssdv_dec_header(ssdv_packet_info_t *info, uint8_t *packet);

#ifdef __cplusplus
}
#endif
#endif

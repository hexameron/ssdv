/*
    Copyright (c) 2015 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of CM256 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include "gf256.h"

#if defined(NO_SIMD)

// Context object for GF(256) math
gf256_ctx GF256Ctx;
static bool Initialized = false;


//-----------------------------------------------------------------------------
// Generator Polynomial

// There are only 16 irreducible polynomials for GF(256)
static const int GF256_GEN_POLY_COUNT = 16;
static const uint8_t GF256_GEN_POLY[GF256_GEN_POLY_COUNT] = {
    0x8e, 0x95, 0x96, 0xa6, 0xaf, 0xb1, 0xb2, 0xb4,
    0xb8, 0xc3, 0xc6, 0xd4, 0xe1, 0xe7, 0xf3, 0xfa,
};

static const int DefaultPolynomialIndex = 3;

// Select which polynomial to use
static void gf255_poly_init(int polynomialIndex)
{
    if (polynomialIndex < 0 || polynomialIndex >= GF256_GEN_POLY_COUNT)
    {
        polynomialIndex = 0;
    }

    GF256Ctx.Polynomial = (GF256_GEN_POLY[polynomialIndex] << 1) | 1;
}


//-----------------------------------------------------------------------------
// Exponential and Log Tables

// Construct EXP and LOG tables from polynomial
static void gf256_explog_init()
{
    unsigned poly = GF256Ctx.Polynomial;
    uint8_t* exptab = GF256Ctx.GF256_EXP_TABLE;
    uint16_t* logtab = GF256Ctx.GF256_LOG_TABLE;

    logtab[0] = 512;
    exptab[0] = 1;
    for (unsigned jj = 1; jj < 255; ++jj)
    {
        unsigned next = (unsigned)exptab[jj - 1] * 2;
        if (next >= 256) next ^= poly;

        exptab[jj] = static_cast<uint8_t>( next );
        logtab[exptab[jj]] = static_cast<uint16_t>( jj );
    }

    exptab[255] = exptab[0];
    logtab[exptab[255]] = 255;

    for (unsigned jj = 256; jj < 2 * 255; ++jj)
    {
        exptab[jj] = exptab[jj % 255];
    }

    exptab[2 * 255] = 1;

    for (unsigned jj = 2 * 255 + 1; jj < 4 * 255; ++jj)
    {
        exptab[jj] = 0;
    }
}


//-----------------------------------------------------------------------------
// Multiply and Divide Tables

// Initialize MUL and DIV tables using LOG and EXP tables
static void gf256_muldiv_init()
{
    // Allocate table memory 65KB x 2
    uint8_t* m = GF256Ctx.GF256_MUL_TABLE;
    uint8_t* d = GF256Ctx.GF256_DIV_TABLE;

    // Unroll y = 0 subtable
    for (int x = 0; x < 256; ++x)
    {
        m[x] = d[x] = 0;
    }

    // For each other y value,
    for (int y = 1; y < 256; ++y)
    {
        // Calculate log(y) for mult and 255 - log(y) for div
        const uint8_t log_y = static_cast<uint8_t>(GF256Ctx.GF256_LOG_TABLE[y]);
        const uint8_t log_yn = 255 - log_y;

        // Next subtable
        m += 256;
        d += 256;

        // Unroll x = 0
        m[0] = 0;
        d[0] = 0;

        // Calculate x * y, x / y
        for (int x = 1; x < 256; ++x)
        {
            uint16_t log_x = GF256Ctx.GF256_LOG_TABLE[x];

            m[x] = GF256Ctx.GF256_EXP_TABLE[log_x + log_y];
            d[x] = GF256Ctx.GF256_EXP_TABLE[log_x + log_yn];
        }
    }
}


//-----------------------------------------------------------------------------
// Inverse Table

// Initialize INV table using DIV table
static void gf256_inv_init()
{
    for (int x = 0; x < 256; ++x)
    {
        GF256Ctx.GF256_INV_TABLE[x] = gf256_div(1, static_cast<uint8_t>(x));
    }
}


//-----------------------------------------------------------------------------
// Initialization

static unsigned char LittleEndianTestData[4] = { 4, 3, 2, 1 };
static bool IsLittleEndian()
{
    return 0x01020304 == *reinterpret_cast<uint32_t*>(LittleEndianTestData);
}

extern "C" int gf256_init_(int version)
{
    if (version != GF256_VERSION)
    {
        // User's header does not match library version.
        return -1;
    }

    // Avoid multiple initialization
    if (Initialized)
    {
        return 0;
    }
    Initialized = true;

    if (!IsLittleEndian())
    {
        // Architecture is not supported (code won't work without mods).
        return -2;
    }

    gf255_poly_init(DefaultPolynomialIndex);
    gf256_explog_init();
    gf256_muldiv_init();
    gf256_inv_init();

    return 0;
}


//-----------------------------------------------------------------------------
// Operations

extern "C" void gf256_add_mem(void * GF256_RESTRICT vx,
                              const void * GF256_RESTRICT vy, int bytes)
{
    uint8_t * GF256_RESTRICT x1 = reinterpret_cast<uint8_t *>(vx);
    const uint8_t * GF256_RESTRICT y1 = reinterpret_cast<const uint8_t *>(vy);

    // Handle bytes
    while (bytes)
    {
        x1[0] ^= y1[0];

        x1++;
        y1++;
        bytes--;
    }
}

extern "C" void gf256_add2_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx,
                               const void * GF256_RESTRICT vy, int bytes)
{
    uint8_t * GF256_RESTRICT z1 = reinterpret_cast<uint8_t *>(vz);
    const uint8_t * GF256_RESTRICT x1 = reinterpret_cast<const uint8_t *>(vx);
    const uint8_t * GF256_RESTRICT y1 = reinterpret_cast<const uint8_t *>(vy);

    // Handle bytes
    while (bytes)
    {
        z1[0] ^= x1[0] ^ y1[0];

        x1++;
        y1++;
        z1++;
        bytes--;
    }
}

extern "C" void gf256_addset_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx,
                                 const void * GF256_RESTRICT vy, int bytes)
{
    uint8_t * GF256_RESTRICT z1 = reinterpret_cast<uint8_t *>(vz);
    const uint8_t * GF256_RESTRICT x1 = reinterpret_cast<const uint8_t *>(vx);
    const uint8_t * GF256_RESTRICT y1 = reinterpret_cast<const uint8_t *>(vy);

    // Handle bytes
    while (bytes)
    {
        z1[0] = x1[0] ^ y1[0];

        x1++;
        y1++;
        z1++;
        bytes--;
    }
}

extern "C" void gf256_muladd_mem(void * GF256_RESTRICT vz, uint8_t y,
                                 const void * GF256_RESTRICT vx, int bytes)
{
    // Use a single if-statement to handle special cases
    if (y <= 1)
    {
        if (y == 1)
        {
            gf256_add_mem(vz, vx, bytes);
        }
        return;
    }

    uint8_t * GF256_RESTRICT z8 = reinterpret_cast<uint8_t*>(vz);
    const uint8_t * GF256_RESTRICT x8 = reinterpret_cast<const uint8_t*>(vx);
    const uint8_t * GF256_RESTRICT table = GF256Ctx.GF256_MUL_TABLE + ((unsigned)y << 8);

    // Handle bytes
    while (bytes)
    {
        z8[0] ^= table[x8[0]];

        x8++;
        z8++;
        bytes--;
    }
}

extern "C" void gf256_mul_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, uint8_t y, int bytes)
{
    // Use a single if-statement to handle special cases
    if (y <= 1)
    {
        if (y == 0)
        {
            memset(vz, 0, bytes);
        }
        return;
    }

    uint8_t * GF256_RESTRICT z8 = reinterpret_cast<uint8_t*>(vz);
    const uint8_t * GF256_RESTRICT x8 = reinterpret_cast<const uint8_t*>(vx);
    const uint8_t * GF256_RESTRICT table = GF256Ctx.GF256_MUL_TABLE + ((unsigned)y << 8);

    // Handle bytes
    while (bytes)
    {
        z8[0] = table[x8[0]];

        x8++;
        z8++;
        bytes--;
    }
}

extern "C" void gf256_memswap(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes)
{
    uint8_t * GF256_RESTRICT x1 = reinterpret_cast<uint8_t *>(vx);
    uint8_t * GF256_RESTRICT y1 = reinterpret_cast<uint8_t *>(vy);

    // Handle bytes
    while (bytes)
    {
        uint8_t temp = x1[0];
        x1[0] = y1[0];
        y1[0] = temp;

        x1++;
        y1++;
        bytes--;
    }
}

#endif

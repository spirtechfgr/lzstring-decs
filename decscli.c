/*
decscli.c - command line around decs.h, for the known answer tests.

Usage: decscli file [bytes]
       decscli -t
Decompresses the file, or its first bytes only, and writes the text to standard
output, one byte per character. Exit status 0 on success; otherwise the diagnostic of
decs is printed on standard error and the exit status is 1.
-t, when built with -DSELFTEST, runs checks of the diagnostics on streams made in
memory: sizes 0 and 3; bits 11, a first codeword of 3; bits 00 01100110 101, the
character f then a codeword of 5 with m = 4; bits 00 01100110 then zeros, a definition
cut short; one character then references to entry 3 up to entry 65442 (accepted) and
65443 (refused). Exit status 0 iff every check passes.

        This file is released under CC0 1.0 Universal; the full text is in LICENSE.

        To the extent possible under law, the author has dedicated all copyright and related
        and neighboring rights to this file to the public domain worldwide.

        You may copy, modify, distribute and perform the work, even for commercial purposes, all
        without asking permission.
*/

#include "decs.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// this host tool assumes an int of at least 32 bits (the 1 MiB buffer, the casts of
// size_t); the check is skipped under C++ and tcc, which lack _Static_assert
#if !defined(__cplusplus) && !defined(__TINYC__)
_Static_assert(sizeof(int) >= 4, "decscli needs an int of at least 32 bits");
#endif

// the consumer: writes inChar, always below 128, as one byte
static int32_t emit(void *inContext, uint16_t inChar)
{
    return putc(inChar, (FILE *)inContext) == EOF ? 1 : 0;
}

#ifdef SELFTEST
// the consumer of the checks: counts the characters
static int32_t count(void *inContext, uint16_t inChar)
{
    (void)inChar;
    ++*(int32_t *)inContext;
    return 0;
}

// appends inCount bits of inValue, least significant first, at bit *inPos of a zeroed
// stream: bit 14 - (i mod 15) of pair i / 15, still without the offset of pack()
static void putBits(uint8_t *inData, int32_t *inPos, int32_t inValue, int32_t inCount)
{
    for (int32_t vJ = 0; vJ < inCount; ++vJ, ++*inPos) {
        int32_t vBit = ((inValue >> vJ) & 1) << (14 - *inPos % 15);   // the bit in its pair
        inData[2 * (*inPos / 15)]     |= (uint8_t)(vBit & 255);
        inData[2 * (*inPos / 15) + 1] |= (uint8_t)(vBit >> 8);
    }
}

// turns the inPos bits into the stream of the specification, v = (bits + 32) mod 32768; its size
static int32_t pack(uint8_t *inData, int32_t inPos)
{
    int32_t vSize = 2 * ((inPos + 14) / 15);   // bytes
    for (int32_t vI = 0; vI < vSize; vI += 2) {
        int32_t vV = ((inData[vI] | inData[vI + 1] << 8) + 32) & 0x7FFF;   // v
        inData[vI]     = (uint8_t)(vV & 255);
        inData[vI + 1] = (uint8_t)(vV >> 8);
    }
    return vSize;
}

// decompresses and checks the diagnostic and the character count; 1 on failure
static int check(const uint8_t *inData, int32_t inSize, int32_t inCode, int32_t inChars, const char *inWhat)
{
    int32_t vChars = 0;                                       // characters delivered
    int32_t vRc    = decs(inData, inSize, count, &vChars);    // result
    if (vRc == inCode && vChars == inChars) return 0;
    printf("FAIL %s: diagnostic %d and %d characters, expected %d and %d\n", inWhat, (int)vRc, (int)vChars, (int)inCode, (int)inChars);
    return 1;
}

// the stream of one character, inRefs references to entry 3 and the end symbol, into inData; its size
static int32_t refStream(uint8_t *inData, int32_t inRefs)
{
    int32_t vPos = 0;   // bits written
    putBits(inData, &vPos, 0, 2);
    putBits(inData, &vPos, 'A', 8);
    for (int32_t vM = 4; vM <= 4 + inRefs; ++vM) {
        int32_t vW = 0;   // bit size of m
        for (int32_t vT = vM; vT; vT >>= 1) ++vW;
        putBits(inData, &vPos, vM < 4 + inRefs ? 3 : 2, vW);
    }
    return pack(inData, vPos);
}

// the built-in checks; exit status
static int selfTest(void)
{
    static uint8_t vData[1 << 18];   // a stream
    int vBad = 0;                    // failures
    int32_t vPos;                    // bits written
    vBad += check(vData, 0, kDecsErrSize, 0, "size 0");
    vBad += check(vData, 3, kDecsErrSize, 0, "size 3");
    memset(vData, 0, 4); vPos = 0; putBits(vData, &vPos, 3, 2);
    vBad += check(vData, pack(vData, vPos), kDecsErrFirst, 0, "first codeword 3");
    memset(vData, 0, 4); vPos = 0; putBits(vData, &vPos, 0, 2); putBits(vData, &vPos, 'f', 8); putBits(vData, &vPos, 5, 3);
    vBad += check(vData, pack(vData, vPos), kDecsErrIndex, 1, "codeword 5 with m = 4");
    memset(vData, 0, 4); vPos = 0; putBits(vData, &vPos, 0, 2); putBits(vData, &vPos, 'f', 8);
    vBad += check(vData, pack(vData, vPos), kDecsErrBits, 1, "definition cut short");
    memset(vData, 0, sizeof vData);
    vBad += check(vData, refStream(vData, 65439), 0, 65440, "entries up to 65442");
    memset(vData, 0, sizeof vData);
    vBad += check(vData, refStream(vData, 65440), kDecsErrCapacity, 65440, "entry 65443");
    printf("%d failure(s)\n", vBad);
    return vBad ? 1 : 0;
}
#endif

// reads the file, decompresses, reports
int main(int argc, char *argv[])
{
    static uint8_t vData[1 << 20];   // the file
#ifdef SELFTEST
    if (argc == 2 && strcmp(argv[1], "-t") == 0) return selfTest();
#endif
    if (argc < 2 || argc > 3) {
        fputs("usage: decscli file [bytes]\n", stderr);
        return 1;
    }
    FILE *vIn = fopen(argv[1], "rb");   // the file
    if (!vIn) {
        perror(argv[1]);
        return 1;
    }
    int32_t vSize = (int32_t)fread(vData, 1, sizeof vData, vIn);   // its size
    fclose(vIn);
    if (vSize == (int32_t)sizeof vData) {
        fprintf(stderr, "%s: larger than %d bytes\n", argv[1], (int)sizeof vData - 1);
        return 1;
    }
    if (argc == 3) {
        char *vEnd;                                                 // end of the number
        int32_t vWanted = (int32_t)strtol(argv[2], &vEnd, 10);     // the truncation
        if (*argv[2] == 0 || *vEnd != 0 || vWanted < 0) {
            fprintf(stderr, "%s: not a byte count\n", argv[2]);
            return 1;
        }
        if (vWanted < vSize) vSize = vWanted;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int32_t vRc = decs(vData, vSize, emit, stdout);   // the diagnostic
    if (fflush(stdout) == EOF) vRc = 1;
    if (vRc != 0) {
        fprintf(stderr, "%d\n", (int)vRc);
        return 1;
    }
    return 0;
}

/*
decs.h - decompressor for LZString compressToUTF16 streams, ASCII output, header-only C11.
Specification: README.md
Public function: decs(). The other functions are internal helpers.

        This file is released under CC0 1.0 Universal; the full text is in LICENSE.

        To the extent possible under law, the author has dedicated all copyright and related
        and neighboring rights to this file to the public domain worldwide.

        You may copy, modify, distribute and perform the work, even for commercial purposes, all
        without asking permission.

    Acknowledgements

        The algorithm is derived from Pieroxy's lz-string https://pieroxy.net/blog/pages/lz-string/index.html
        and checked against code at or linked at GitHub repository https://github.com/pieroxy/lz-string

        It borrows from LZW of Welch, T. A. (1984). "A Technique for High-Performance Data Compression."
        Computer, 17(6): 8-19. https://doi.org/10.1109/MC.1984.1659158
*/

#ifndef DECS_H
#define DECS_H

#include <stdint.h>
#include <stdlib.h>

// character range and its size
enum { kDecsMinC = 32, kDecsMaxC = 127, kDecsNC = kDecsMaxC - kDecsMinC + 1 };

// the bit reader's accumulator when it holds no bit: the sentinel alone, at bit 15
#define KDECSEMPTY 0x8000   // not an enumerator, for portability to compilers with 16-bit int

// diagnostics
enum {
    kDecsErrSize     = 29300,   // k odd or below 2
    kDecsErrBits     = 29301,   // input exhausted while reading a codeword or a character
    kDecsErrIndex    = 29302,   // x > m
    kDecsErrFirst    = 29303,   // x = m with p = 0, a first codeword of 3
    kDecsErrCapacity = 29304,   // m > 65442, more entries than a 16-bit entry represents
    kDecsErrMemory   = 29305    // realloc failed, or the array would exceed 32760 bytes with a 16-bit size_t
};

// consumer of the decompressed text, one character per call; non-zero aborts and is returned
typedef int32_t (*tDecsConsumer)(void *inContext, uint16_t inChar);

// decompression state: bit reader (the next bit at bit 15 of fAcc, a sentinel below,
// KDECSEMPTY when empty) and dictionary
struct tDecs {
    const uint8_t *fData;       // compressed data
    int32_t        fPos;        // next byte to fetch
    int32_t        fSize;       // bytes in fData
    uint32_t       fAcc;        // accumulator
    uint16_t      *fDict;       // the dictionary, entry j at position j - 3
    int32_t        fCapacity;   // entries allocated
};

// reads inCount bits, least significant first; -1 when the input is exhausted
static inline int32_t decsRead(struct tDecs *inS, int32_t inCount)
{
    int32_t vX = 0;   // result
    for (int32_t vJ = 0; vJ < inCount; ++vJ) {
        if (inS->fAcc == KDECSEMPTY) {
            if (inS->fPos >= inS->fSize) return -1;
            uint32_t vV = inS->fData[inS->fPos] | (uint32_t)inS->fData[inS->fPos + 1] << 8;   // v
            inS->fPos += 2;
            inS->fAcc = (((vV + 32736) & 0x7FFF) << 1) | 1;
        }
        vX |= (int32_t)(inS->fAcc >> 15) << vJ;
        inS->fAcc = (inS->fAcc << 1) & 0xFFFF;
    }
    return vX;
}

// stores inG as entry inM, growing the dictionary when needed; 0 or a diagnostic
static inline int32_t decsStore(struct tDecs *inS, int32_t inM, int32_t inG)
{
    if (inM - 3 + kDecsNC > UINT16_MAX) return kDecsErrCapacity;
    if (inM - 3 >= inS->fCapacity) {
        int32_t vLeft = inS->fSize - inS->fPos;                    // compressed bytes not yet fetched
        int32_t vGrow = (vLeft < 6 ? 6 : vLeft) - 4;               // bytes added, at least 2
        int32_t vBytes = 2 * inS->fCapacity + vGrow;               // new size of the array
        if (sizeof(size_t) < 4 && vBytes > 32760) return kDecsErrMemory;   // beyond a 16-bit size_t
        uint16_t *vNew = (uint16_t *)realloc(inS->fDict, (size_t)vBytes);   // the grown array; the cast is for C++ includers
        if (!vNew) return kDecsErrMemory;
        inS->fDict      = vNew;
        inS->fCapacity += vGrow / 2;
    }
    inS->fDict[inM - 3] = (uint16_t)inG;
    return 0;
}

// dictionary index represented by entry inJ, which holds an index
static inline int32_t decsIndex(const uint16_t *inDict, int32_t inJ)
{
    return (int32_t)inDict[inJ - 3] - kDecsNC + 3;
}

// source slot of entry inJ (a concatenation): the neighbour holding its prefix
static inline int32_t decsSource(const uint16_t *inDict, int32_t inJ)
{
    return (inJ - 1 == 3 || inDict[inJ - 4] >= kDecsNC) ? inJ - 1 : inJ - 2;
}

// first character of entry inJ, as a g value
static inline int32_t decsFirst(const uint16_t *inDict, int32_t inJ)
{
    while (inDict[inJ - 3] >= kDecsNC) {
        int32_t vS = decsSource(inDict, inJ);   // source slot
        inJ = vS == 3 ? 3 : decsIndex(inDict, vS);
    }
    return (int32_t)inDict[inJ - 3];
}

// outputs the string of entry inJ through the consumer, the trie serving as stack:
// source slots are overwritten with child links on the way down to the root and
// restored on the way up; returns the consumer's first non-zero value, else 0
static inline int32_t decsOutput(uint16_t *inDict, int32_t inJ, tDecsConsumer inConsumer, void *inContext)
{
    int32_t vCur   = inJ;   // node being visited
    int32_t vPrev  = 0;     // node visited before it, 0 for none
    int32_t vPrev2 = 0;     // node visited before that
    while (inDict[vCur - 3] >= kDecsNC) {              // descent to the root
        int32_t vS   = decsSource(inDict, vCur);                                  // source slot
        int32_t vPar = vS == 3 ? 3 : decsIndex(inDict, vS);                         // prefix
        if (vS != 3) inDict[vS - 3] = (uint16_t)((vPrev ? vPrev : vCur) - 3 + kDecsNC);
        vPrev2 = vPrev;
        vPrev  = vCur;
        vCur   = vPar;
    }
    int32_t vR     = (int32_t)inDict[vCur - 3];   // the root character
    int32_t vC     = vR;                          // character to emit
    int32_t vChild = vPrev;                       // node above the current one
    for (;;) {                                    // ascent, emitting
        int32_t vRc = inConsumer(inContext, (uint16_t)(vC + kDecsMinC));   // consumer's verdict
        if (vRc != 0) return vRc;
        if (vCur == inJ) return 0;
        int32_t vParent = vCur;                   // node just emitted
        vCur = vChild;
        int32_t vS = decsSource(inDict, vCur);    // source slot, holding the child link
        if (vS != 3) {
            vChild          = decsIndex(inDict, vS);
            inDict[vS - 3]  = (uint16_t)(vParent - 3 + kDecsNC);
        } else {
            vChild = vPrev2;
        }
        if (vCur != inJ && decsSource(inDict, vChild) == vCur) vC = vR;   // own slot overwritten: a self-reference
        else vC = decsFirst(inDict, decsIndex(inDict, vCur));
    }
}

// the decompression proper
static inline int32_t decsRun(struct tDecs *inS, tDecsConsumer inConsumer, void *inContext)
{
    int32_t vM = 3;   // next dictionary index
    int32_t vP = 0;   // index of the previous string appended, 0 for none
    for (;;) {
        int32_t vW = 0;   // bit size of m
        for (int32_t vT = vM; vT; vT >>= 1) ++vW;
        int32_t vX = decsRead(inS, vW);   // the codeword
        if (vX < 0) return kDecsErrBits;
        if (vX > vM) return kDecsErrIndex;
        if (vX == 2) return 0;
        if (vX == vM && vP == 0) return kDecsErrFirst;
        if (vX <= 1) {                            // character definition
            int32_t vY = decsRead(inS, 8 * vX + 8);   // the character
            if (vY < 0) return kDecsErrBits;
            if (vY < kDecsMinC) vY = kDecsMinC;
            else if (vY > kDecsMaxC) vY = kDecsMaxC;
            int32_t vRc = decsStore(inS, vM, vY - kDecsMinC);   // diagnostic
            if (vRc != 0) return vRc;
            vX = vM++;
        }
        if (vP != 0) {                            // concatenation entry: stores x, a self-reference when x = m
            int32_t vRc = decsStore(inS, vM, vX - 3 + kDecsNC);   // diagnostic
            if (vRc != 0) return vRc;
            ++vM;
        }
        vP = vX;
        int32_t vRc = decsOutput(inS->fDict, vP, inConsumer, inContext);   // consumer's verdict
        if (vRc != 0) return vRc;
    }
}

// Decompresses inSize bytes at inCompressed, delivering the text to inConsumer one
// character per call with inContext. Returns 0 on success, the consumer's non-zero
// value, or a kDecsErr* diagnostic.
static inline int32_t decs(const uint8_t *inCompressed,   // compressed data
                           int32_t        inSize,         // its size in bytes, k
                           tDecsConsumer  inConsumer,     // receives the decompressed text
                           void          *inContext)      // passed unchanged to inConsumer
{
    if (inSize < 2 || inSize % 2) return kDecsErrSize;
    struct tDecs vS = { .fData = inCompressed, .fPos = 0, .fSize = inSize, .fAcc = KDECSEMPTY,
                        .fDict = NULL, .fCapacity = 0 };                                 // the state, no dictionary yet
    int32_t vRc = decsRun(&vS, inConsumer, inContext);                                  // result
    free(vS.fDict);
    return vRc;
}

#endif /* DECS_H */

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

// decompressed character range [kDecsMinC, kDecsMaxC] and its size kDecsNC
//  32 (ASCII space) folds all JSON whitespace to space, which is equivalent
// 127 (ASCII DEL  ) folds all other non-ASCII UTF-16 to DEL
enum { kDecsMinC = 32, kDecsMaxC = 127, kDecsNC = kDecsMaxC - kDecsMinC + 1 };

// diagnostics
enum {
    kDecsErrSize     = 29300,   // k odd or below 2
    kDecsErrBits     = 29301,   // input exhausted while reading a codeword or a character
    kDecsErrIndex    = 29302,   // x > m
    kDecsErrFirst    = 29303,   // x = m with p = 0, a first codeword of 3
    kDecsErrCapacity = 29304,   // too many dictionary entries for a 16-bit representation
    kDecsErrMemory   = 29305    // realloc failed, or the array would exceed 32760 bytes with a 16-bit size_t
};

// consumer of the decompressed text, one character per call; non-zero aborts and is returned
typedef int32_t (*tDecsConsumer)(void *inContext, uint16_t inChar);

// decompression state: bit reader (the next bit at bit 31 of fAcc, then a sentinel
// bit; when that reaches bit 31 it is time to fetch two bytes) and dictionary
struct tDecs {
    const uint8_t *fData;       // compressed data
    int32_t        fPos;        // next byte to fetch
    int32_t        fSize;       // bytes in fData
    uint32_t       fAcc;        // accumulator, rightmost bit set is a sentinel
    uint16_t      *fDict;       // the dictionary, entry j at position j - 3; the code below works in positions
    int32_t        fCapacity;   // entries allocated
};

// Reads inCount bits, least significant first; -1 when the input is exhausted
static inline int32_t decsRead(struct tDecs *inS, int32_t inCount)
{
    int32_t  vJ = inCount;   // bits still to read
    uint32_t vX = 0;         // result, bits shifted in from bit 31; all bits will be shifted out
    do {                     // inCount times, at least 2
        vX = (vX >> 1) | (inS->fAcc & 0x80000000);  // add a bit, possibly the sentinel
        if ((inS->fAcc <<= 1) == 0) { // left shift the accumulator and test if that removed the sentinel
            if (inS->fPos >= inS->fSize) return -1;
            // get 2 new bytes little-endian, undo the offset by 32, ignore bit 15, add sentinel, align left
            inS->fAcc = (((inS->fData[inS->fPos] | (uint32_t)inS->fData[inS->fPos + 1] << 8) - 32) << 1 | 1) << 16;
            vX &= inS->fAcc | 0x7FFFFFFF; // replace the sentinel
            inS->fAcc <<= 1;
            inS->fPos += 2;
        }
    } while (--vJ);
    return (int32_t)(vX >> (32 - inCount));
}

// Stores inG at position inP, growing the dictionary when needed; 0 or a diagnostic
static inline int32_t decsStore(struct tDecs *inS, int32_t inP, int32_t inG)
{
    if (inP > UINT16_MAX - kDecsNC) return kDecsErrCapacity;
    if (inP >= inS->fCapacity) {
        int32_t vGrow = ((inS->fSize - inS->fPos) >> 1) - 2;    // compressed pairs of bytes not yet fetched, minus 2
        inS->fCapacity += vGrow > 0 ? vGrow : 1;                // entries after the growth, always strictly positive
        // fCapacity is now wrong, but either we'll exit with some error and the caller will free fDict, or we'll have grown
        if (sizeof(size_t) < 4 && inS->fCapacity > 16380) return kDecsErrMemory;    // beyond a 16-bit size_t
        uint16_t *vNew = (uint16_t *)realloc(inS->fDict, (size_t)(inS->fCapacity << 1));   // the grown array; the cast is for C++ includers
        if (!vNew) return kDecsErrMemory;
        inS->fDict     = vNew;
    }
    inS->fDict[inP] = (uint16_t)inG;
    return 0;
}

// Position of the entry that the entry at inP represents, which holds an index
static inline int32_t decsIndex(const uint16_t *inDict, int32_t inP)
{
    return (int32_t)inDict[inP] - kDecsNC;
}

// Source slot of the entry at inP (a concatenation): the neighbour holding its prefix
static inline int32_t decsSource(const uint16_t *inDict, int32_t inP)
{
    --inP;
    return inP - (inP && inDict[inP] < kDecsNC);
}

// First character of the entry at inP, as a g value
static inline int32_t decsFirst(const uint16_t *inDict, int32_t inP)
{
    while (inDict[inP] >= kDecsNC) {
        int32_t vS = decsSource(inDict, inP);   // source slot
        inP = vS == 0 ? 0 : decsIndex(inDict, vS);
    }
    return (int32_t)inDict[inP];
}

// Outputs the string of the entry at inP through the consumer, the trie serving as
// stack: source slots are overwritten with child links on the way down to the root and
// restored on the way up; returns the consumer's first non-zero value, else 0
static inline int32_t decsOutput(uint16_t *inDict, int32_t inP, tDecsConsumer inConsumer, void *inContext)
{
    int32_t vCur   = inP;   // node being visited
    int32_t vPrev  = inP;   // node visited before it; the node itself at first, whose slot then gets a link to itself
    int32_t vPrev2 = inP;   // node visited before that; read only once two nodes have been visited
    while (inDict[vCur] >= kDecsNC) {                  // descent to the root
        int32_t vS   = decsSource(inDict, vCur);                // source slot
        int32_t vPar = vS == 0 ? 0 : decsIndex(inDict, vS);     // prefix
        if (vS != 0) inDict[vS] = (uint16_t)(vPrev + kDecsNC);
        vPrev2 = vPrev;
        vPrev  = vCur;
        vCur   = vPar;
    }
    int32_t vR     = (int32_t)inDict[vCur];       // the root character
    int32_t vC     = vR;                          // character to emit
    int32_t vChild = vPrev;                       // node above the current one
    for (;;) {                                    // ascent, emitting
        int32_t vRc = inConsumer(inContext, (uint16_t)(vC + kDecsMinC));   // consumer's verdict
        if (vRc != 0) return vRc;
        if (vCur == inP) return 0;
        int32_t vParent = vCur;                   // node just emitted
        vCur = vChild;
        int32_t vS = decsSource(inDict, vCur);    // source slot, holding the child link
        if (vS != 0) {
            vChild     = decsIndex(inDict, vS);
            inDict[vS] = (uint16_t)(vParent + kDecsNC);
        } else {
            vChild = vPrev2;
        }
        if (vCur != inP && decsSource(inDict, vChild) == vCur) vC = vR;   // own slot overwritten: a self-reference
        else vC = decsFirst(inDict, decsIndex(inDict, vCur));
    }
}

// The decompression proper
static inline int32_t decsRun(struct tDecs *inS, tDecsConsumer inConsumer, void *inContext)
{
    int32_t vN     = 0;    // entries in the dictionary, m - 3, the position of the next one
    int32_t vP     = -1;   // position of the previous string appended, -1 for none
    int32_t vW     = 2;    // bit size of m, the codeword width
    int32_t vLimit = 0;    // 2^vW - 4 used to detect when we need one more bit
    for (;;) {
        if (vN > vLimit) { // m grows by at most 2 per iteration, so one step suffices
            ++vW;
            vLimit += vLimit + 4; // maintain vLimit = 2^vW - 4
        }
        int32_t vX = decsRead(inS, vW);   // the codeword, then the position it designates
        if (vX < 0) return kDecsErrBits;
        if ((vX -= 3) < 0) { // convert for 0-based dictionary, and detect special cases 0/1/2
            if (vX == 2 - 3) return 0;  // final symbol 2
            int32_t vY = decsRead(inS, (vX + (3 + 1)) << 3);   // definition of that character
            if (vY < 0) return kDecsErrBits;
            if (vY < kDecsMinC) vY = kDecsMinC;                 // clamp, low limit
            else if (vY > kDecsMaxC) vY = kDecsMaxC;            // clamp, high limit
            int32_t vRc = decsStore(inS, vN, vY - kDecsMinC);   // diagnostic
            if (vRc != 0) return vRc;
            vX = vN++;
        } else { // reference to a dictionary entry
            if (vX >= vN) {
                if (vX != vN) return kDecsErrIndex;
                if (vP < 0) return kDecsErrFirst;
            }
        }
        if (vP >= 0) {                            // concatenation entry: stores x, a self-reference when x = m
            int32_t vRc = decsStore(inS, vN, vX + kDecsNC);   // diagnostic
            if (vRc != 0) return vRc;
            ++vN;
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
    if (inSize <= 0 || (inSize & 1)) return kDecsErrSize;
    struct tDecs vS = { .fData = inCompressed, .fPos = 0, .fSize = inSize,
                        .fAcc = 0x80000000, .fDict = NULL, .fCapacity = 0 };    // the state, no dictionary yet
    int32_t vRc = decsRun(&vS, inConsumer, inContext);                  // result
    free(vS.fDict);
    return vRc;
}

#endif /* DECS_H */

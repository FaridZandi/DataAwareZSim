/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <iomanip>
#include <fstream>
#include "cache_arrays.h"
#include "hash.h"
#include "repl_policies.h"

/* Set-associative array implementation */

SetAssocArray::SetAssocArray(uint32_t _numLines,
                             uint32_t _assoc,
                             ReplPolicy *_rp,
                             HashFamily *_hf) : rp(_rp), hf(_hf),
                                                numLines(_numLines),
                                                assoc(_assoc) {
    array = gm_calloc<Address>(numLines);
    numSets = numLines / assoc;
    setMask = numSets - 1;
    assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);
}

int32_t SetAssocArray::lookup(const Address lineAddr, const MemReq *req, bool updateReplacement) {
    uint32_t set = hf->hash(0, lineAddr) & setMask;
    uint32_t first = set * assoc;
    for (uint32_t id = first; id < first + assoc; id++) {
        if (array[id] == lineAddr) {
            if (updateReplacement) rp->update(id, req);
            return id;
        }
    }
    return -1;
}

uint32_t SetAssocArray::preinsert(const Address lineAddr, const MemReq *req,
                                  Address *wbLineAddr, char *wbLineValue) { //TODO: Give out valid bit of wb cand?
    uint32_t set = hf->hash(0, lineAddr) & setMask;
    uint32_t first = set * assoc;

    uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

    *wbLineAddr = array[candidate];
    return candidate;
}

void SetAssocArray::postinsert(const Address lineAddr, const MemReq *req, uint32_t candidate) {
    rp->replaced(candidate);
    array[candidate] = lineAddr;
    rp->update(candidate, req);
}

DataAwareSetAssocArray::DataAwareSetAssocArray(uint32_t _numLines,
                                               uint32_t _lineSize,
                                               uint32_t _assoc,
                                               ReplPolicy *_rp,
                                               HashFamily *_hf) : SetAssocArray(_numLines, _assoc, _rp, _hf),
                                                                  lineSize(_lineSize) {
    values = gm_calloc<void *>(numLines);
    dirty = gm_calloc<bool *>(numLines);

    for (unsigned int i = 0; i < numLines; ++i) {
        values[i] = gm_calloc<char>(_lineSize);
        dirty[i] = gm_calloc<bool>(_lineSize);
    }
}

#include <fstream>
std::ofstream saed("trace2.txt");

static VOID EmitMem(VOID *ea, INT32 size, int offset) {
    ea = (void*)((uintptr_t)ea + offset);
    saed << " with size: " << std::dec << setw(3) << size << " with value ";
    switch (size) {
        case 0:
            cerr << "zero length data here" << std::endl;
            saed << setw(1);
            break;

        case 1:
            saed << static_cast<UINT32>(*static_cast<UINT8 *>(ea));
            break;

        case 2:
            saed << *static_cast<UINT16 *>(ea);
            break;

        case 4:
            saed << *static_cast<UINT32 *>(ea);
            break;

        case 8:
            saed << *static_cast<UINT64 *>(ea);
            break;

        default:
            saed << setw(1) << "0x";
            for (INT32 i = 0; i < size; i++) {
                saed << setfill('0') << setw(2) << static_cast<UINT32>(static_cast<UINT8 *>(ea)[i]);
            }
            saed << std::setfill(' ');
            break;
    }
    saed << std::endl;
}

void DataAwareSetAssocArray::postinsert(const Address lineAddr, const MemReq *req, uint32_t candidate) {
    SetAssocArray::postinsert(lineAddr, req, candidate);
    memcpy(values[candidate], req->value, lineSize);

    saed << "0x" << setw(15) << std::hex << std::left << (array[candidate] << lineBits) + req->line_offset << " ";
    EmitMem(values[candidate], req->size, req->line_offset);

    for (unsigned int i = 0; i < lineSize; ++i) {
        dirty[candidate][i] = false;
    }
}



void DataAwareSetAssocArray::updateValue(void* value, UINT32 size, unsigned int offset, uint32_t candidate) {
    unsigned int writeSize = MIN(lineSize - offset, size);
    void* dst = (void*)((uintptr_t)(values[candidate]) + offset);
    void* src = (void*)((uintptr_t)(value) + offset);
    memcpy(dst, src, writeSize);

    for (unsigned int i = offset; i < writeSize + offset; ++i) {
        dirty[candidate][i] = true;
    }

//    saed << "0x" << setw(15) << std::hex << std::left << (array[candidate] << lineBits) + offset << " ";
//    EmitMem(dst, size, 0);
}


uint32_t
DataAwareSetAssocArray::preinsert(const Address lineAddr, const MemReq *req, Address *wbLineAddr, char *wbLineValue) {
    uint32_t candidate = SetAssocArray::preinsert(lineAddr, req, wbLineAddr, wbLineValue);
    memcpy(wbLineValue, values[candidate], lineSize);

    int count = 0;
    for (unsigned int i = 0; i < lineSize; ++i) {
        if(dirty[candidate][i]){
            count ++;
        }
    }
//    std::cerr << count << std::endl;

    return candidate;
}


/* ZCache implementation */

ZArray::ZArray(uint32_t _numLines, uint32_t _ways, uint32_t _candidates, ReplPolicy *_rp,
               HashFamily *_hf) //(int _size, int _lineSize, int _assoc, int _zassoc, ReplacementPolicy<T>* _rp, int _hashType)
        : rp(_rp), hf(_hf), numLines(_numLines), ways(_ways), cands(_candidates) {
    assert_msg(ways > 1, "zcaches need >=2 ways to work");
    assert_msg(cands >= ways, "candidates < ways does not make sense in a zcache");
    assert_msg(numLines % ways == 0, "number of lines is not a multiple of ways");

    //Populate secondary parameters
    numSets = numLines / ways;
    assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);
    setMask = numSets - 1;

    lookupArray = gm_calloc<uint32_t>(numLines);
    array = gm_calloc<Address>(numLines);
    for (uint32_t i = 0; i < numLines; i++) {
        lookupArray[i] = i;  // start with a linear mapping; with swaps, it'll get progressively scrambled
    }
    swapArray = gm_calloc<uint32_t>(cands / ways + 2);  // conservative upper bound (tight within 2 ways)
}

void ZArray::initStats(AggregateStat *parentStat) {
    AggregateStat *objStats = new AggregateStat();
    objStats->init("array", "ZArray stats");
    statSwaps.init("swaps", "Block swaps in replacement process");
    objStats->append(&statSwaps);
    parentStat->append(objStats);
}

int32_t ZArray::lookup(const Address lineAddr, const MemReq *req, bool updateReplacement) {
    /* Be defensive: If the line is 0, panic inst, char** wbLineValueead of asserting. Now this can
     * only happen on a segfault in the main program, but when we move to full
     * system, phy page 0 might be used, and this will hit us in a very subtle
     * way if we don't check.
     */
    if (unlikely(!lineAddr)) panic("ZArray::lookup called with lineAddr==0 -- your app just segfaulted");

    for (uint32_t w = 0; w < ways; w++) {
        uint32_t lineId = lookupArray[w * numSets + (hf->hash(w, lineAddr) & setMask)];
        if (array[lineId] == lineAddr) {
            if (updateReplacement) {
                rp->update(lineId, req);
            }
            return lineId;
        }
    }
    return -1;
}

uint32_t ZArray::preinsert(const Address lineAddr, const MemReq *req, Address *wbLineAddr, char *wbLineValue) {
    ZWalkInfo candidates[cands + ways]; //extra ways entries to avoid checking on every expansion

    bool all_valid = true;
    uint32_t fringeStart = 0;
    uint32_t numCandidates = ways; //seeds

    //info("Replacement for incoming 0x%lx", lineAddr);

    //Seeds
    for (uint32_t w = 0; w < ways; w++) {
        uint32_t pos = w * numSets + (hf->hash(w, lineAddr) & setMask);
        uint32_t lineId = lookupArray[pos];
        candidates[w].set(pos, lineId, -1);
        all_valid &= (array[lineId] != 0);
        //info("Seed Candidate %d addr 0x%lx pos %d lineId %d", w, array[lineId], pos, lineId);
    }

    //Expand fringe in BFS fashion
    while (numCandidates < cands && all_valid) {
        uint32_t fringeId = candidates[fringeStart].lineId;
        Address fringeAddr = array[fringeId];
        assert(fringeAddr);
        for (uint32_t w = 0; w < ways; w++) {
            uint32_t hval = hf->hash(w, fringeAddr) & setMask;
            uint32_t pos = w * numSets + hval;
            uint32_t lineId = lookupArray[pos];

            // Logically, you want to do this...
#if 0
            if (lineId != fringeId) {
                //info("Candidate %d way %d addr 0x%lx pos %d lineId %d parent %d", numCandidates, w, array[lineId], pos, lineId, fringeStart);
                candidates[numCandidates++].set(pos, lineId, (int32_t)fringeStart);
                all_valid &= (array[lineId] != 0);
            }
#endif
            // But this compiles as a branch and ILP sucks (this data-dependent branch is long-latency and mispredicted often)
            // Logically though, this is just checking for whether we're revisiting ourselves, so we can eliminate the branch as follows:
            candidates[numCandidates].set(pos, lineId, (int32_t) fringeStart);
            all_valid &= (array[lineId] !=
                          0);  // no problem, if lineId == fringeId the line's already valid, so no harm done
            numCandidates += (lineId != fringeId); // if lineId == fringeId, the cand we just wrote will be overwritten
        }
        fringeStart++;
    }

    //Get best candidate (NOTE: This could be folded in the code above, but it's messy since we can expand more than zassoc elements)
    assert(!all_valid || numCandidates >= cands);
    numCandidates = (numCandidates > cands) ? cands : numCandidates;

    //info("Using %d candidates, all_valid=%d", numCandidates, all_valid);

    uint32_t bestCandidate = rp->rankCands(req, ZCands(&candidates[0], &candidates[numCandidates]));
    assert(bestCandidate < numLines);

    //Fill in swap array

    //Get the *minimum* index of cands that matches lineId. We need the minimum in case there are loops (rare, but possible)
    uint32_t minIdx = -1;
    for (uint32_t ii = 0; ii < numCandidates; ii++) {
        if (bestCandidate == candidates[ii].lineId) {
            minIdx = ii;
            break;
        }
    }
    assert(minIdx >= 0);
    //info("Best candidate is %d lineId %d", minIdx, bestCandidate);

    lastCandIdx = minIdx; //used by timing simulation code to schedule array accesses

    int32_t idx = minIdx;
    uint32_t swapIdx = 0;
    while (idx >= 0) {
        swapArray[swapIdx++] = candidates[idx].pos;
        idx = candidates[idx].parentIdx;
    }
    swapArrayLen = swapIdx;
    assert(swapArrayLen > 0);

    //Write address of line we're replacing
    *wbLineAddr = array[bestCandidate];

    return bestCandidate;
}

void ZArray::postinsert(const Address lineAddr, const MemReq *req, uint32_t candidate) {
    //We do the swaps in lookupArray, the array stays the same
    assert(lookupArray[swapArray[0]] == candidate);
    for (uint32_t i = 0; i < swapArrayLen - 1; i++) {
        //info("Moving position %d (lineId %d) <- %d (lineId %d)", swapArray[i], lookupArray[swapArray[i]], swapArray[i+1], lookupArray[swapArray[i+1]]);
        lookupArray[swapArray[i]] = lookupArray[swapArray[i + 1]];
    }
    lookupArray[swapArray[swapArrayLen -
                          1]] = candidate; //note that in preinsert() we walk the array backwards when populating swapArray, so the last elem is where the new line goes
    //info("Inserting lineId %d in position %d", candidate, swapArray[swapArrayLen-1]);

    rp->replaced(candidate);
    array[candidate] = lineAddr;
    rp->update(candidate, req);

    statSwaps.inc(swapArrayLen - 1);
}

CompressedDataAwareSetAssoc::CompressedDataAwareSetAssoc(uint32_t _numLines, uint32_t _lineSize, uint32_t _assoc,
                                                         ReplPolicy *_rp, HashFamily *_hf) : DataAwareSetAssocArray(
        _numLines, _lineSize, _assoc, _rp, _hf) {}


std::ofstream fuck("fuck.txt");

uint32_t CompressedDataAwareSetAssoc::preinsert(const Address lineAddr, const MemReq *req, Address *wbLineAddr,
                                                char *wbLineValue) {

    int candidate = SetAssocArray::preinsert(lineAddr, req, wbLineAddr, wbLineValue);
    memcpy(wbLineValue, (char*) values[candidate], lineSize);

    char* memValue = new char[lineSize];
    PIN_SafeCopy(memValue, (void*)(*wbLineAddr << lineBits), lineSize);

    if(*wbLineAddr){
        bool equal = true;

        for(unsigned int i = 0; i < lineSize; i++) {
            if(memValue[i] != wbLineValue[i]){
                equal = false;
            }
        }

        if(!equal){
            fuck << "ridim   " << std::hex << " " << *wbLineAddr << std::endl;
        } else {
            fuck << "naridim " << std::hex << " " << *wbLineAddr << std::endl;
        }
    }

    return candidate;
}

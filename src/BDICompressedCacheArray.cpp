//
// Created by farid on 4/25/18.
//

#include <fstream>
#include "BDICompressedCacheArray.h"
#include "bithacks.h"
#include "hash.h"
#include "repl_policies.h"
#include "BDILRUReplPolicy.h"


BDICompressedCacheArray::BDICompressedCacheArray(uint32_t _numLines, uint32_t _lineSize, uint32_t _assoc,
                                                 ReplPolicy *_rp, HashFamily *_hf) : lineSize(_lineSize), rp(_rp),
                                                                                     hf(_hf), numLines(_numLines),
                                                                                     assoc(_assoc) {

    array = gm_calloc<Address>(numLines);
    numSets = numLines / assoc;
    setMask = numSets - 1;
    assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);

    uncompressed_values = gm_calloc<void *>(numLines);
    compressed_sizes = gm_calloc<uint32_t>(numLines);

    for (unsigned int i = 0; i < numLines; ++i) {
        uncompressed_values[i] = gm_calloc<char>(lineSize);
    }
}

int32_t BDICompressedCacheArray::lookup(const Address lineAddr, const MemReq *req, bool updateReplacement) {
    uint32_t set = (uint32_t) (hf->hash(0, lineAddr) & setMask);
    uint32_t first = set * assoc;
    for (uint32_t id = first; id < first + assoc; id++) {
        if (array[id] == lineAddr) {
            if (updateReplacement) rp->update(id, req);
            return id;
        }
    }
    return -1;
}

std::ofstream BDILog("bdi.txt");
PIN_LOCK lock2;

uint32_t
BDICompressedCacheArray::BDIpreinsert(const Address lineAddr, const MemReq *req, uint32_t compressed_size,
                                      Address *wbLineAddrs, char **wbLineValues, uint32_t *evicted_lines) {

//    PIN_GetLock(&lock2, 2);
//    BDILog << "--------------------------------------------------" << std::endl;
//    BDILog << "-----------going to find the evictions------------" << std::endl;
//    BDILog << "--------------------------------------------------" << std::endl;

    uint32_t eviction_index = 0;

    uint32_t set = (uint32_t) (hf->hash(0, lineAddr) & setMask);
    uint32_t first = set * assoc;

    uint32_t max_size = assoc / 2 * lineSize;
    uint32_t current_size = 0;

//    BDILog << first << " to " << first + assoc << std::endl;

    for (uint32_t id = first; id < first + assoc; id++) {
//        BDILog << compressed_sizes[id] << " ";
        current_size += compressed_sizes[id];
    }

//    BDILog << std::endl;
//    BDILog << "current sum of compressed sizes : " << current_size << std::endl;
//    BDILog << "maximum possible size : " << max_size << std::endl;
//    BDILog << "compressed size of the new line : " << compressed_size << std::endl << std::endl;

    uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

    wbLineAddrs[eviction_index] = array[candidate];
    memcpy(wbLineValues[eviction_index], uncompressed_values[candidate], lineSize);
    evicted_lines[eviction_index] = candidate;

    current_size -= compressed_sizes[candidate];

    eviction_index++;

//    BDILog << "found victim with id : " << candidate << std::endl;
//    BDILog << "found victim with size : " << compressed_sizes[candidate] << std::endl;
//    BDILog << "current size would be : " << current_size << std::endl;
//    BDILog << "including the new line : " << current_size + compressed_size << std::endl << std::endl;

    if (current_size + compressed_size > max_size) {
        rp->buildCandsPriorityQueue(first, first + assoc);
    }

    while (current_size + compressed_size > max_size) {
        candidate = rp->getNextCand();

        wbLineAddrs[eviction_index] = array[candidate];
        memcpy(wbLineValues[eviction_index], uncompressed_values[candidate], lineSize);
        evicted_lines[eviction_index] = candidate;

        current_size -= compressed_sizes[candidate];

        eviction_index++;

//        BDILog << "found victim with id : " << candidate << std::endl;
//        BDILog << "found victim with size : " << compressed_sizes[candidate] << std::endl;
//        BDILog << "current size would be : " << current_size << std::endl;
//        BDILog << "including the new line : " << current_size + compressed_size << std::endl << std::endl;
    }

//    PIN_ReleaseLock(&lock2);

    return eviction_index;
}

void BDICompressedCacheArray::BDIpostinsert(const Address lineAddr, const MemReq *req, uint32_t candidate,
                                            uint32_t compressed_size) {
    rp->replaced(candidate);
    array[candidate] = lineAddr;
    compressed_sizes[candidate] = compressed_size;
    memcpy(uncompressed_values[candidate], req->value, lineSize);
    rp->update(candidate, req);
}

uint32_t BDICompressedCacheArray::BDIupdateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate,
                                                 uint32_t compressed_size, Address *wbLineAddrs, char **wbLineValues,
                                                 uint32_t *evicted_lines) {

    unsigned int writeSize = MIN(lineSize - offset, size);
    void *dst = (void *) ((uintptr_t) (uncompressed_values[candidate]) + offset);
    void *src = (void *) ((uintptr_t) (value) + offset);
    memcpy(dst, src, writeSize);
    compressed_sizes[candidate] = compressed_size;

    uint32_t first = (candidate / assoc) * assoc;
    uint32_t max_size = assoc / 2 * lineSize;
    uint32_t current_size = 0;
    for (uint32_t id = first; id < first + assoc; id++) {
        current_size += compressed_sizes[id];
    }

    if (current_size <= max_size) {
        return 0;
    } else {
        uint32_t eviction_index = 0;
        uint32_t evicted_line = rp->rankCands(NULL, SetAssocCands(first, first + assoc));

        if (evicted_line != candidate) {
            wbLineAddrs[eviction_index] = array[evicted_line];
            memcpy(wbLineValues[eviction_index], uncompressed_values[evicted_line], lineSize);
            evicted_lines[eviction_index] = evicted_line;

            current_size -= compressed_sizes[evicted_line];

            eviction_index++;
        }

        if (current_size > max_size) {
            rp->buildCandsPriorityQueue(first, first + assoc);
        }

        while (current_size > max_size) {
            evicted_line = rp->getNextCand();

            if (evicted_line != candidate) {
                wbLineAddrs[eviction_index] = array[evicted_line];
                memcpy(wbLineValues[eviction_index], uncompressed_values[evicted_line], lineSize);
                evicted_lines[eviction_index] = evicted_line;

                current_size -= compressed_sizes[evicted_line];

                eviction_index++;
            }
        }

        return eviction_index;
    }
}

uint32_t BDICompressedCacheArray::getAssoc() {
    return assoc;
}

void BDICompressedCacheArray::unsetCompressedSizes(uint32_t id) {
    compressed_sizes[id] = 0;
}




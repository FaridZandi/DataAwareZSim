//
// Created by farid on 5/1/18.
//

#include "BDICompressedCacheArray.h"
#include "hash.h"
#include "repl_policies.h"

BDICompressedCacheArray::BDICompressedCacheArray(uint32_t _numLines, uint32_t _lineSize,
                                                 uint32_t _assoc, ReplPolicy *_rp,
                                                 HashFamily *_hf) : rp(_rp), hf(_hf),
                                                                    numLines(_numLines),
                                                                    assoc(_assoc), lineSize(_lineSize) {
    array = gm_calloc<Address>(numLines);
    numSets = numLines / assoc;
    setMask = numSets - 1;
    assert_msg(isPow2(numSets), "must have a power of 2 # sets, but you specified %d", numSets);

    compressed_sizes = gm_calloc<uint32_t>(numLines);
    uncompressed_values = gm_calloc<void *>(numLines);

    for (unsigned int i = 0; i < numLines; ++i) {
        uncompressed_values[i] = gm_calloc<char>(_lineSize);
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

uint32_t BDICompressedCacheArray::preinsert(const Address lineAddr, const MemReq *req,
                                            Address *wbLineAddr,
                                            char *wbLineValue) { //TODO: Give out valid bit of wb cand?


    uint32_t set = (uint32_t) (hf->hash(0, lineAddr) & setMask);
    uint32_t first = set * assoc;

    uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

    *wbLineAddr = array[candidate];
    memcpy(wbLineValue, uncompressed_values[candidate], lineSize);

    return candidate;
}

void BDICompressedCacheArray::postinsert(const Address lineAddr, const MemReq *req, uint32_t candidate) {
    rp->replaced(candidate);
    array[candidate] = lineAddr;
    rp->update(candidate, req);
    memcpy(uncompressed_values[candidate], req->value, lineSize);
}

void BDICompressedCacheArray::updateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate) {
    unsigned int writeSize = MIN(lineSize - offset, size);
    void *dst = (void *) ((uintptr_t) (uncompressed_values[candidate]) + offset);
    void *src = (void *) ((uintptr_t) (value) + offset);
    memcpy(dst, src, writeSize);
}

uint32_t BDICompressedCacheArray::getAssoc() const {
    return assoc;
}

uint32_t BDICompressedCacheArray::BDIpreinsert(const Address lineAddr, const MemReq *req, uint32_t compressed_size,
                                               Address *wbLineAddrs, char **wbLineValues, uint32_t *evicted_lines) {


    uint32_t eviction_index = 0;

    uint32_t set = (uint32_t) (hf->hash(0, lineAddr) & setMask);
    uint32_t first = set * assoc;

    uint32_t max_size = assoc / 2 * lineSize;
    uint32_t current_size = 0;

    for (uint32_t id = first; id < first + assoc; id++) {
        current_size += compressed_sizes[id];
    }

    uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

    wbLineAddrs[eviction_index] = array[candidate];
    evicted_lines[eviction_index] = candidate;
    memcpy(wbLineValues[eviction_index], uncompressed_values[candidate], lineSize);

    current_size -= compressed_sizes[candidate];

    eviction_index++;

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
    }

    return eviction_index;
}

void BDICompressedCacheArray::BDIpostinsert(const Address lineAddr, const MemReq *req, uint32_t lineId,
                                            uint32_t compressed_size) {
    rp->replaced(lineId);
    array[lineId] = lineAddr;
    memcpy(uncompressed_values[lineId], req->value, lineSize);
    compressed_sizes[lineId] = compressed_size;
    rp->update(lineId, req);
}

uint32_t BDICompressedCacheArray::BDIupdateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate,
                                                 uint32_t compressed_size, Address *wbLineAddrs, char **wbLineValues,
                                                 uint32_t *evicted_lines) {
//    unsigned int writeSize = MIN(lineSize - offset, size);
//    void *dst = (void *) ((uintptr_t) (uncompressed_values[candidate]) + offset);
//    void *src = (void *) ((uintptr_t) (value) + offset);

    memcpy(uncompressed_values[candidate], value, lineSize);

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

void BDICompressedCacheArray::unsetCompressedSizes(uint32_t id) {
    compressed_sizes[id] = 0;
}
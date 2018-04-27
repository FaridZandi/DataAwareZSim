//
// Created by farid on 4/25/18.
//

#include "BDICompressedCache.h"
#include <fstream>
#include "cache.h"
#include "hash.h"

#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "BDICompressedCacheArray.h"


BDICompressedCache::BDICompressedCache(uint32_t _numLines, CC *_cc, CacheArray *_array, ReplPolicy *_rp,
                                       uint32_t _accLat, uint32_t _invLat, const g_string &_name) : Cache(_numLines,
                                                                                                          _cc, _array,
                                                                                                          _rp, _accLat,
                                                                                                          _invLat,
                                                                                                          _name) {}

uint64_t BDICompressedCache::access(MemReq &req) {

    uint64_t respCycle = req.cycle;
    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)



    if (likely(!skipAccess)) {
        BDICompressedCacheArray * bdi_array = (BDICompressedCacheArray*) array;

        bool updateReplacement = (req.type == GETS) || (req.type == GETX);
        int32_t lineId = bdi_array->lookup(req.lineAddr, &req, updateReplacement);
        int32_t lookupLineId = lineId;
        respCycle += accLat;

        if (lineId == -1 && cc->shouldAllocate(req)) {

            uint32_t compressed_size = BDICompress((char *) req.value);

            uint32_t assoc = bdi_array->getAssoc();
            uint32_t *evicted_lines = new uint32_t[assoc];
            Address *wbLineAddrs = new Address[assoc];
            char **wbLineValues = new char *[assoc];
            for (int i = 0; i < assoc; ++i) {
                wbLineValues[i] = new char[(1U << lineBits)];
            }

            unsigned int eviction_count = bdi_array->BDIpreinsert(req.lineAddr, &req, compressed_size,
                                                              wbLineAddrs, wbLineValues,
                                                              evicted_lines); //find the lineIds to replace

            trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

            //Evictions are not in the critical path in any sane implementation -- we do not include their delays
            //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do

            for (int k = 0; k < eviction_count; ++k) {
                cc->processEviction(req, wbLineAddrs[k], wbLineValues[k], evicted_lines[k], respCycle);
                //1. if needed, send invalidates/downgrades to lower level //hereeeee
                bdi_array->unsetCompressedSizes(evicted_lines[k]);
            }

            bdi_array->BDIpostinsert(req.lineAddr, &req, evicted_lines[0], compressed_size);
            //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.

            delete[] evicted_lines;
            delete[] wbLineAddrs;
            for (int j = 0; j < assoc; ++j) {
                delete[] wbLineValues[j];
            }
            delete[] wbLineValues;

        }

        // SMF : when storing, if the lineAddr is present in the array, the value should be updated.
        if (lookupLineId != -1) {
            if (req.type == GETX or req.type == PUTS or req.type == PUTX) {
                bdi_array->updateValue(req.value, req.size, req.line_offset, lookupLineId);
            }
        }

        // Enforce single-record invariant: Writeback access may have a timing
        // record. If so, read it.
        EventRecorder *evRec = zinfo->eventRecorders[req.srcId];
        TimingRecord wbAcc;
        wbAcc.clear();
        if (unlikely(evRec && evRec->hasRecord())) {
            wbAcc = evRec->popRecord();
        }

        respCycle = cc->processAccess(req, lineId, respCycle);

        // Access may have generated another timing record. If *both* access
        // and wb have records, stitch them together
        if (unlikely(wbAcc.isValid())) {
            if (!evRec->hasRecord()) {
                // Downstream should not care about endEvent for PUTs
                wbAcc.endEvent = nullptr;
                evRec->pushRecord(wbAcc);
            } else {
                // Connect both events
                TimingRecord acc = evRec->popRecord();
                assert(wbAcc.reqCycle >= req.cycle);
                assert(acc.reqCycle >= req.cycle);
                DelayEvent *startEv = new(evRec) DelayEvent(0);
                DelayEvent *dWbEv = new(evRec) DelayEvent(wbAcc.reqCycle - req.cycle);
                DelayEvent *dAccEv = new(evRec) DelayEvent(acc.reqCycle - req.cycle);
                startEv->setMinStartCycle(req.cycle);
                dWbEv->setMinStartCycle(req.cycle);
                dAccEv->setMinStartCycle(req.cycle);
                startEv->addChild(dWbEv, evRec)->addChild(wbAcc.startEvent, evRec);
                startEv->addChild(dAccEv, evRec)->addChild(acc.startEvent, evRec);

                acc.reqCycle = req.cycle;
                acc.startEvent = startEv;
                // endEvent / endCycle stay the same; wbAcc's endEvent not connected
                evRec->pushRecord(acc);
            }
        }
    }

    cc->endAccess(req);

    assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
               name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);
    return respCycle;
}


unsigned long long BDICompressedCache::my_llabs(long long x) {
    unsigned long long t = (unsigned long long int) (x >> 63);
    return (x ^ t) - t;
}

long long unsigned *BDICompressedCache::convertBuffer2Array(char *buffer, unsigned size, unsigned step) {
    long long unsigned *values = (long long unsigned *) malloc(sizeof(long long unsigned) * size / step);

    unsigned int i, j;
    for (i = 0; i < size / step; i++) {
        values[i] = 0;
    }

    for (i = 0; i < size; i += step) {
        for (j = 0; j < step; j++) {
            // assuming little-endianness
            values[i / step] += (long long unsigned) ((unsigned char) buffer[i + j]) << (8 * j);
        }
    }
    return values;
}

int BDICompressedCache::isZeroPackable(long long unsigned *values, unsigned size) {
    int zero = 1;
    unsigned int i;
    for (i = 0; i < size; i++) {
        if (values[i] != 0) {
            zero = 0;
            break;
        }
    }
    return zero;
}

int BDICompressedCache::isSameValuePackable(long long unsigned *values, unsigned size) {
    int same = 1;
    unsigned int i;
    for (i = 0; i < size; i++) {
        if (values[0] != values[i]) {
            same = 0;
            break;
        }
    }
    return same;
}

unsigned BDICompressedCache::multBaseCompression(long long unsigned *values, unsigned size,
                                                 unsigned limit_bits, unsigned bsize) {
    unsigned long long limit = 0;
    unsigned BASES = 2;

    //define the appropriate size for the mask
    switch (limit_bits) {
        case 1:
            limit = 0xFF;
            break;
        case 2:
            limit = 0xFFFF;
            break;
        case 4:
            limit = 0xFFFFFFFF;
            break;
        default:
            std::cerr << "wrong limit_bits" << std::endl;
            exit(1);
    }

    unsigned long long mbases[64];
    unsigned baseCount = 1;
    mbases[0] = 0;
    unsigned int i, j;
    for (i = 0; i < size; i++) {
        for (j = 0; j < baseCount; j++) {
            if (my_llabs((long long int) (mbases[j] - values[i])) > limit) {
                mbases[baseCount++] = values[i];
            }
        }
        if (baseCount >= BASES) //we don't have more bases
            break;
    }

    // find how many elements can be compressed with mbases
    unsigned compCount = 0;
    for (i = 0; i < size; i++) {
        //ol covered = 0;
        for (j = 0; j < baseCount; j++) {
            if (my_llabs((long long int) (mbases[j] - values[i])) <= limit) {
                compCount++;
                break;
            }
        }
    }

    if (compCount < size)
        return size * bsize;

    return (limit_bits * compCount) + (bsize * BASES) + ((size - compCount) * bsize);
}

unsigned BDICompressedCache::BDICompress(char *buffer) {
    unsigned int lineSize = (1U << lineBits);
    long long unsigned *values = convertBuffer2Array(buffer, lineSize, 8);
    unsigned bestCSize = lineSize;
    unsigned currCSize = lineSize;

    if (isZeroPackable(values, lineSize / 8))
        bestCSize = 1;

    if (isSameValuePackable(values, lineSize / 8))
        currCSize = 8;

    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 8, 1, 8);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 8, 2, 8);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 8, 4, 8);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    delete[] values;

    values = convertBuffer2Array(buffer, lineSize, 4);

    if (isSameValuePackable(values, lineSize / 4))
        currCSize = 4;

    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 4, 1, 4);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 4, 2, 4);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    delete[] values;

    values = convertBuffer2Array(buffer, lineSize, 2);

    currCSize = multBaseCompression(values, lineSize / 2, 1, 2);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    delete[] values;

    return bestCSize;
}


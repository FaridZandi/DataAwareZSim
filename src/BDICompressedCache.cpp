//
// Created by farid on 5/1/18.
//

#include <fstream>
#include <iomanip>
#include "BDICompressedCache.h"
#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "BDICompressedCacheArray.h"

// std::ofstream updateValueLog("update_values_log.log");

BDICompressedCache::BDICompressedCache(uint32_t _numLines, CC *_cc,
                                       CacheArray *_array, ReplPolicy *_rp,
                                       uint32_t _accLat, uint32_t _invLat,
                                       const g_string &_name) : Cache(_numLines, _cc, _array, _rp,
                                                                      _accLat, _invLat, _name) {

}


uint64_t l2_stat_counter = 0;
uint64_t llc_stat_counter = 0;

uint64_t BDICompressedCache::access(MemReq &req) {
    uint64_t respCycle = req.cycle;

    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)
    if (likely(!skipAccess)) {

        BDICompressedCacheArray *bdi_array = (BDICompressedCacheArray *) array;


        if (strcmp(getName(), "l2-0") == 0) {
            l2_stat_counter ++;
            if(l2_stat_counter % 10000 == 0) {
                l2_sum_full = bdi_array->getFullLinesNum();
                l2_sum_all = bdi_array->getMaxLinesNum();


                std::cerr << "l2 sum full  :  " << l2_sum_full << std::endl;
                std::cerr << "l2 sum all   :  " << l2_sum_all  << std::endl;
                std::cerr << "l2 comp ratio:  " << (double) l2_sum_full / l2_sum_all << std::endl;
                std::cerr << "llc sum full :  " << llc_sum_full << std::endl;
                std::cerr << "llc sum all  :  " << llc_sum_all  << std::endl;
                std::cerr << "llc comp ratio:  " << (double) llc_sum_full / llc_sum_all << std::endl;
                std::cerr << "---------------------------------------------------------" << std::endl;
                std::cerr << "---------------------------------------------------------" << std::endl;
            }
        }

        if (strcmp(getName(), "llc-0") == 0) {
            llc_stat_counter ++;
            if(llc_stat_counter % 10000 == 0) {
                llc_sum_full = bdi_array->getFullLinesNum();
                llc_sum_all = bdi_array->getMaxLinesNum();


                std::cerr << "l2 sum full  :  " << l2_sum_full << std::endl;
                std::cerr << "l2 sum all   :  " << l2_sum_all  << std::endl;
                std::cerr << "l2 comp ratio:  " << (double) l2_sum_full / l2_sum_all << std::endl;
                std::cerr << "llc sum full :  " << llc_sum_full << std::endl;
                std::cerr << "llc sum all  :  " << llc_sum_all  << std::endl;
                std::cerr << "llc comp ratio:  " << (double) llc_sum_full / llc_sum_all << std::endl;
                std::cerr << "---------------------------------------------------------" << std::endl;
                std::cerr << "---------------------------------------------------------" << std::endl;
            }
        }



        bool updateReplacement = (req.type == GETS) || (req.type == GETX);
        int32_t lineId = bdi_array->lookup(req.lineAddr, &req, updateReplacement);
        respCycle += accLat;
        int32_t lookupLineId = lineId;


        if (lineId == -1 && cc->shouldAllocate(req)) {
            //Make space for new line

            uint32_t compressed_size = BDICompress((char *) req.value);

            uint32_t assoc = bdi_array->getAssoc();

            uint32_t *evicted_lines = gm_calloc<uint32_t>(assoc);
            Address *wbLineAddrs = gm_calloc<Address>(assoc);

            char **wbLineValues = gm_calloc<char *>(assoc);
            for (uint32_t i = 0; i < assoc; ++i) {
                wbLineValues[i] = gm_calloc<char>((1U << lineBits));
            }
            uint32_t eviction_count = bdi_array->BDIpreinsert(req.lineAddr, &req, compressed_size,
                                                              wbLineAddrs, wbLineValues,
                                                              evicted_lines); //find the lineId to replace
            lineId = evicted_lines[eviction_count - 1];

            for (uint32_t j = 0; j < eviction_count; ++j) {
                //Evictions are not in the critical path in any sane implementation -- we do not include their delays
                //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do

                cc->processEviction(req, wbLineAddrs[j], wbLineValues[j], evicted_lines[j],
                                    respCycle); //1. if needed, send invalidates/downgrades to lower level //hereeeee
                bdi_array->unsetCompressedSizes(evicted_lines[j]);
            }

            gm_free(evicted_lines);
            gm_free(wbLineAddrs);
            for (uint32_t i = 0; i < assoc; ++i) {
                gm_free(wbLineValues[i]);
            }
            gm_free(wbLineValues);

            bdi_array->BDIpostinsert(req.lineAddr, &req, lineId,
                                     compressed_size); //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.
        }

        if (lookupLineId != -1) { // so the line was present in the cache in the first place.
            if (req.type == PUTS or req.type == PUTX) {
                updateValues(req, respCycle, bdi_array, lookupLineId);
            } else if (req.type == GETS) {
                if (cc->isValid(lookupLineId)) {
                    if (bdi_array->isCompressed(lookupLineId)) {
                        // if the line is compressed it has to decompressed before it is handed to lower levels.
//                        respCycle += DecompressionLat;
                    }
                } else {
                    updateValues(req, respCycle, bdi_array, lookupLineId);
                }
            } else if(req.type == GETX){
                if (cc->isValid(lookupLineId)) {
                    if (bdi_array->isCompressed(lookupLineId)) {
                        // if the line is compressed it has to decompressed before it is handed to lower levels.
//                        respCycle += DecompressionLat;
                    }
                }
                updateValues(req, respCycle, bdi_array, lookupLineId);
            } else {
                panic("?!");
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

void BDICompressedCache::updateValues(const MemReq &req, uint64_t respCycle, BDICompressedCacheArray *bdi_array,
                                      int32_t lookupLineId) {

    uint32_t assoc = bdi_array->getAssoc();
    uint32_t *evicted_lines = gm_calloc<uint32_t>(assoc);
    Address *wbLineAddrs = gm_calloc<Address>(assoc);

    char **wbLineValues = gm_calloc<char *>(assoc);
    for (uint32_t i = 0; i < assoc; ++i) {
        wbLineValues[i] = gm_calloc<char>((1U << lineBits));
    }

    uint32_t compressed_size = BDICompress((char *) req.value);

    uint32_t eviction_count = bdi_array->BDIupdateValue(req.value, req.size, req.line_offset, lookupLineId,
                                                        compressed_size, wbLineAddrs, wbLineValues,
                                                        evicted_lines);

    for (uint32_t j = 0; j < eviction_count; ++j) {
        cc->processEviction(req, wbLineAddrs[j], wbLineValues[j], evicted_lines[j],
                            respCycle); //1. if needed, send invalidates/downgrades to lower level //here !

        bdi_array->unsetCompressedSizes(evicted_lines[j]);
    }

    gm_free(evicted_lines);
    gm_free(wbLineAddrs);

    for (uint32_t i = 0; i < assoc; ++i) {
        gm_free(wbLineValues[i]);
    }
    gm_free(wbLineValues);
}

uint64_t BDICompressedCache::finishInvalidate(const InvReq &req) {
    int32_t lineId = array->lookup(req.lineAddr, nullptr, false);
    assert_msg(lineId != -1, "[%s] Invalidate on non-existing address 0x%lx type %s lineId %d, reqWriteback %d",
               name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
    uint64_t respCycle = req.cycle + invLat;

    BDICompressedCacheArray *bdi_array = (BDICompressedCacheArray *) array;

    bdi_array->unsetCompressedSizes(lineId);

    trace(Cache, "[%s] Invalidate start 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr,
          InvTypeName(req.type), lineId, *req.writeback);
    respCycle = cc->processInv(req, lineId,
                               respCycle); //send invalidates or downgrades to children, and adjust our own state
    trace(Cache, "[%s] Invalidate end 0x%lx type %s lineId %d, reqWriteback %d, latency %ld", name.c_str(),
          req.lineAddr, InvTypeName(req.type), lineId, *req.writeback, respCycle - req.cycle);

    return respCycle;
}

unsigned long long BDICompressedCache::my_llabs(long long x) {
    unsigned long long t = (unsigned long long int) (x >> 63);
    return (x ^ t) - t;
}

void BDICompressedCache::convertBuffer2Array(char *buffer, unsigned size, unsigned step,
                                             long long unsigned *values) {
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
    long long unsigned values[64];

    unsigned int lineSize = (1U << lineBits);
    convertBuffer2Array(buffer, lineSize, 8, values);
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


    convertBuffer2Array(buffer, lineSize, 4, values);

    if (isSameValuePackable(values, lineSize / 4))
        currCSize = 4;

    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 4, 1, 4);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    currCSize = multBaseCompression(values, lineSize / 4, 2, 4);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    convertBuffer2Array(buffer, lineSize, 2, values);

    currCSize = multBaseCompression(values, lineSize / 2, 1, 2);
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    return bestCSize;
}
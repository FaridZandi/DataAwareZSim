//
// Created by farid on 5/1/18.
//

#ifndef DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H
#define DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H

#include "cache.h"
#include "BDICompressedCacheArray.h"

class BDICompressedCache : public Cache{
    static const uint64_t DecompressionLat = 1;

public:
    BDICompressedCache(uint32_t _numLines, CC *_cc, CacheArray *_array, ReplPolicy *_rp, uint32_t _accLat,
                       uint32_t _invLat, const g_string &_name);

    virtual uint64_t access(MemReq &req) override;

    static unsigned long long my_llabs(long long x);

    static void convertBuffer2Array(char *buffer, unsigned size, unsigned step,
                                    long long unsigned *values);

    static int isZeroPackable(long long unsigned *values, unsigned size);

    static int isSameValuePackable(long long unsigned *values, unsigned size);

    static unsigned multBaseCompression(long long unsigned *values, unsigned size, unsigned limit_bits, unsigned bsize);

    unsigned BDICompress(char *buffer);

protected:
    virtual uint64_t finishInvalidate(const InvReq &req) override;

    void updateValues(const MemReq &req, uint64_t respCycle, BDICompressedCacheArray *bdi_array, int32_t lookupLineId);

};






#endif //DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H
//
// Created by farid on 4/25/18.
//

#ifndef DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H
#define DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H

#include "cache.h"

class BDICompressedCache : public Cache{


public:
    BDICompressedCache(uint32_t _numLines, CC *_cc, CacheArray *_array, ReplPolicy *_rp, uint32_t _accLat,
                       uint32_t _invLat, const g_string &_name);

    virtual uint64_t access(MemReq &req) override;

    static unsigned long long my_llabs(long long x);

    static long long unsigned *convertBuffer2Array(char *buffer, unsigned size, unsigned step);

    static int isZeroPackable(long long unsigned *values, unsigned size);

    static int isSameValuePackable(long long unsigned *values, unsigned size);

    static unsigned multBaseCompression(long long unsigned *values, unsigned size, unsigned limit_bits, unsigned bsize);

    unsigned BDICompress(char *buffer);

};


#endif //DATA_AWARE_ZSIM_BDICOMPRESSEDCACHE_H

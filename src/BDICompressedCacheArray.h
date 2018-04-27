//
// Created by farid on 4/25/18.
//

#ifndef DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H
#define DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H

#include <iostream>
#include "cache_arrays.h"

class BDICompressedCacheArray : public CacheArray {
protected :
    Address *array;
    uint32_t *compressed_sizes;
    void **uncompressed_values;

    uint32_t lineSize;
    ReplPolicy *rp;
    HashFamily *hf;
    uint32_t numLines;
    uint32_t numSets;
public:
    virtual uint32_t getAssoc() override;

protected:
    uint32_t assoc;
    uint32_t setMask;

public:
    BDICompressedCacheArray(uint32_t _numLines, uint32_t _lineSize, uint32_t _assoc, ReplPolicy *_rp, HashFamily *_hf);

    virtual int32_t lookup(const Address lineAddr, const MemReq *req, bool updateReplacement) override;

    uint32_t BDIpreinsert(const Address lineAddr, const MemReq *req, uint32_t compressed_size,
                                  Address *wbLineAddrs, char ** wbLineValues, uint32_t * evicted_lines);

    void BDIpostinsert(const Address lineAddr, const MemReq *req, uint32_t lineId,
                                   uint32_t compressed_size);

    void BDIupdateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate);

    void unsetCompressedSizes(uint32_t id);

    virtual uint32_t preinsert(const Address lineAddr, const MemReq *req, Address *wbLineAddr, char *wbLineValue,
                               uint32_t compressed_size) override {return 0;};

    virtual void postinsert(const Address lineAddr, const MemReq *req, uint32_t lineId) override {};
};


#endif //DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H

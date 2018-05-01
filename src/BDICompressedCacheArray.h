//
// Created by farid on 5/1/18.
//

#ifndef DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H
#define DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H


#include "cache_arrays.h"

class BDICompressedCacheArray : public CacheArray {
protected:
    Address *array;
    ReplPolicy *rp;
    HashFamily *hf;
    uint32_t numLines;
    uint32_t numSets;
    uint32_t assoc;
protected:
    uint32_t setMask;
    void **uncompressed_values;
    uint32_t *compressed_sizes;
    uint32_t lineSize;

public:
    BDICompressedCacheArray(uint32_t _numLines, uint32_t _lineSize, uint32_t _assoc, ReplPolicy *_rp, HashFamily *_hf);

    int32_t lookup(const Address lineAddr, const MemReq *req, bool updateReplacement);

    uint32_t preinsert(const Address lineAddr, const MemReq *req, Address *wbLineAddr, char *wbLineValue);

    virtual void postinsert(const Address lineAddr, const MemReq *req, uint32_t candidate);

    virtual void updateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate) override;

    uint32_t BDIpreinsert(const Address lineAddr, const MemReq *req, uint32_t compressed_size,
                          Address *wbLineAddrs, char **wbLineValues, uint32_t *evicted_lines);

    void BDIpostinsert(const Address lineAddr, const MemReq *req, uint32_t lineId, uint32_t compressed_size);

    uint32_t BDIupdateValue(void *value, UINT32 size, unsigned int offset, uint32_t candidate, uint32_t compressed_size,
                            Address *wbLineAddrs, char **wbLineValues, uint32_t *evicted_lines);

    uint32_t getAssoc() const;

    void unsetCompressedSizes(uint32_t id);
};


#endif //DATA_AWARE_ZSIM_BDICOMPRESSEDCACHEARRAY_H

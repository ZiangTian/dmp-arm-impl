// src/mem/cache/prefetch/dmp.hh
#ifndef __MEM_CACHE_PREFETCH_DMP_HH__
#define __MEM_CACHE_PREFETCH_DMP_HH__

#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct DMPPrefetcherParams;

namespace prefetch
{

class DMP : public Queued
{
  protected:
    const bool chasePointers;
    std::vector<Addr> historyTable;
    const unsigned historyTableSize;

    Tick lastPrefetchTick;
    Tick resetThreshold;

    bool inHistoryTable(Addr target_addr);

    bool allowPageCrossing() const override { return true; }
    
    const ByteOrder byteOrder;

  public:
    // Constructor taking the params struct
    DMP(const DMPPrefetcherParams &p);
    ~DMP() override = default;

    /**
     * Called on L2 fills, after L2 misses, the data is served from memory
     */
    void notifyFill(const CacheAccessProbeArg &acc) override;

    /**
     * Called on L2 hits, by which time the data is available from L2
     */
    void notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi) override;


    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
    
    /**
     * Reset the history table
     * if curtick - lastPrefetchTick > threshold, reset the history table
     */
    void checkResetHistoryTable();
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_DMP_HH__

Excellent questions! Let me walk through this systematically.

1. How Different Probes Invoke Different DMP Methods
Looking at the probe connection in base.cc:270:

```
listeners.push_back(probeManager->connect<PrefetchListener>(
    *this, "Miss", false, true));   // isFill=false, miss=true
listeners.push_back(probeManager->connect<PrefetchListener>(
    *this, "Fill", true, false));   // isFill=TRUE, miss=false  ← KEY!
listeners.push_back(probeManager->connect<PrefetchListener>(
    *this, "Hit", false, false));   // isFill=false, miss=false
```

The PrefetchListener constructor stores the isFill flag:

```
PrefetchListener(Base &_parent, std::string name, bool _isFill = false, ...)
    : parent(_parent), isFill(_isFill), ...
{}
```

Then when any probe fires, it calls:

```
void PrefetchListener::notify(const CacheAccessProbeArg &arg)
{
    if (isFill) {
        parent.notifyFill(arg);  // ← "Fill" probe goes HERE
    } else {
        parent.probeNotify(arg, miss);  // ← "Hit"/"Miss" probes go HERE
    }
}
```

So the routing is:

ppFill→notify() → isFill=true → DMP::notifyFill() ✓
ppHit→notify() → isFill=false → Base::probeNotify() → DMP::notify() ✓
ppMiss→notify() → isFill=false → Base::probeNotify() → DMP::notify() (filtered out by pfi.isCacheMiss() check)


2. Why notifyFill() Filters Out Reads and HW Prefetches
Looking at your code:

```
void DMP::notifyFill(const CacheAccessProbeArg &acc)
{
    const PacketPtr &pkt = acc.pkt;
    
    // Only process demand reads (not writes or HW prefetches)
    if (!pkt->isRead() || pkt->cmd.isHWPrefetch()) {
        return;
    }
    // ...
}
```

Why !pkt->isRead()?

Fills can be triggered by writes (write allocate policy)
DMP only cares about pointer-chasing reads, not write data
Write data won't contain useful pointers to prefetch
Why pkt->cmd.isHWPrefetch()?

A prefetch response from your own DMP will also trigger ppFill
This creates an infinite loop: DMP prefetch → fill → notifyFill → scan → DMP prefetch → ...
You must exclude HW prefetches to prevent scanning your own prefetched data
The Full Logic:

```
void DMP::notifyFill(const CacheAccessProbeArg &acc)
{
    const PacketPtr &pkt = acc.pkt;
    
    // Filter 1: Only reads (not writes)
    if (!pkt->isRead()) {
        return;  // Writes don't have pointer data we care about
    }
    
    // Filter 2: Only DEMAND reads (not our own prefetches)
    if (pkt->cmd.isHWPrefetch()) {
        return;  // Don't scan our own prefetched data (infinite loop!)
    }
    
    // This is a DEMAND read that missed L2 and was filled from memory
    // Now scan the filled data for pointers
    PrefetchInfo pfi(pkt, pkt->getAddr(), false);
    Queued::notify(acc, pfi);
}
```

3. What Information Cache Passes to Prefetcher
Currently, the cache passes CacheAccessProbeArg:

```
struct CacheAccessProbeArg
{
    PacketPtr pkt;         // The request/response packet
    CacheAccessor &accessor;  // Reference to the cache
};
```

And then PrefetchInfo is created:

```
PrefetchInfo::PrefetchInfo(PacketPtr pkt, Addr addr, bool miss)
{
    // Copies ONLY the packet's data, NOT the entire cache block!
    unsigned int req_size = pkt->req->getSize();
    if ((!write && miss) || !pkt->hasData()) {
        data = nullptr;
    } else {
        data = new uint8_t[req_size];
        Addr offset = pkt->req->getPaddr() - pkt->getAddr();
        std::memcpy(data, &(pkt->getConstPtr<uint8_t>()[offset]), req_size);
        //                                                  ^^^^^^^^
        //                                            ONLY packet size!
    }
}
```

The Problem: The packet may only contain part of the cache block (e.g., 8 bytes), but you need the entire 64-byte cache block to scan for pointers!

4. Solution: Pass Entire Cache Block to DMP
You need to modify the code to pass the full cache block data. Here's how:

Step 1: Modify notify() and notifyFill() to Access Cache Block

```
void
DMP::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi)
{
    // Filter: Only L2 HITS (not misses)
    if (pfi.isCacheMiss()) {
        return;
    }
    
    // Get the cache block from accessor
    CacheBlk *blk = acc.accessor.tags->findBlock(
        {pfi.getAddr(), pfi.isSecure()});
    
    if (!blk || !blk->isValid()) {
        DPRINTF(HWPrefetch, "DMP::notify - block not found or invalid\n");
        return;
    }
    
    DPRINTF(HWPrefetch, "DMP::notify - L2 HIT, scanning full cache block\n");
    
    // Scan the ENTIRE cache block (not just packet data)
    scanCacheBlock(blk, pfi.getAddr());
}

void
DMP::notifyFill(const CacheAccessProbeArg &acc)
{
    const PacketPtr &pkt = acc.pkt;
    
    // Filter 1: Only demand reads
    if (!pkt->isRead() || pkt->cmd.isHWPrefetch()) {
        return;
    }
    
    // Get the cache block that was just filled
    CacheBlk *blk = acc.accessor.tags->findBlock(
        {pkt->getAddr(), pkt->isSecure()});
    
    if (!blk || !blk->isValid()) {
        DPRINTF(HWPrefetch, "DMP::notifyFill - block not found after fill\n");
        return;
    }
    
    DPRINTF(HWPrefetch, "DMP::notifyFill - L2 FILL, scanning full cache block\n");
    
    // Scan the ENTIRE cache block (not just packet data)
    scanCacheBlock(blk, pkt->getAddr());
}

void
DMP::scanCacheBlock(CacheBlk *blk, Addr trigger_addr)
{
    // blk->data contains the full 64-byte cache block
    uint8_t *block_data = blk->data;
    const size_t block_size = blkSize;  // 64 bytes
    
    DPRINTF(HWPrefetch, "DMP: Scanning block addr=%#x, size=%d bytes\n",
            regenerateBlkAddr(blk), block_size);
    
    // Scan every 8-byte aligned position for potential pointers
    for (size_t offset = 0; offset < block_size; offset += sizeof(uint64_t)) {
        uint64_t potential_ptr;
        std::memcpy(&potential_ptr, block_data + offset, sizeof(uint64_t));
        
        // Convert to correct byte order
        potential_ptr = byteOrder == ByteOrder::little ? 
            letoh(potential_ptr) : betoh(potential_ptr);
        
        DPRINTF(HWPrefetch, "  Offset %d: value=%#x\n", offset, potential_ptr);
        
        // Check if this looks like a valid pointer
        if (isValidPointer(potential_ptr)) {
            Addr prefetch_addr = potential_ptr & ~(Addr)(blkSize - 1);
            
            // Check history table
            if (!inHistoryTable(prefetch_addr)) {
                DPRINTF(HWPrefetch, "  -> Issuing prefetch for %#x\n", 
                        prefetch_addr);
                
                std::vector<AddrPriority> addresses;
                addresses.push_back(AddrPriority(prefetch_addr, 0));
                
                // Issue the prefetch
                pfIdentified++;
                for (const auto &addr : addresses) {
                    addToQueue(addr.first, addr.second);
                }
                
                // Add to history table
                unsigned idx = (prefetch_addr / blkSize) % historyTableSize;
                historyTable[idx] = prefetch_addr;
            }
        }
    }
}

bool
DMP::isValidPointer(uint64_t ptr)
{
    // Check if pointer is in valid address range
    // Typically user space is 0x0000'0000'0000'0000 to 0x0000'7fff'ffff'ffff
    // Filter out obvious non-pointers
    
    if (ptr == 0) return false;  // NULL pointer
    if (ptr < 0x1000) return false;  // Low addresses (likely integers)
    if (ptr > 0x0000'7fff'ffff'ffff) return false;  // Kernel space / non-canonical
    
    // Additional heuristic: check alignment
    if ((ptr & 0x7) != 0) return false;  // Unaligned (likely not a pointer)
    
    return true;
}
```

Step 2: Update dmp.hh


Final todo:

update the stats correctly. have claude do it. 

scale up the test program so we get a difference.
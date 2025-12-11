DMP Final Implementation: Handling L2 Hits and Fills
The Complete Picture
DMP must trigger when L1 misses and L2 is accessed, regardless of whether L2 hits or misses:

```Scenario 1: L1 Miss → L2 Hit
L1 miss → L2 access → L2 HIT (data in L2)
    ↓
ppHit->notify() fires
    ↓
DMP::notify() called ✓
    ↓
Scans data from L2
Scenario 2: L1 Miss → L2 Miss
L1 miss → L2 access → L2 MISS (data not in L2)
    ↓
Fetch from memory/L3
    ↓
handleFill() - writes data to L2
    ↓
ppFill->notify() fires
    ↓
DMP::notifyFill() called ✓
    ↓
Scans data just filled into L2
```

Implementation

```notify()
 - Handles L2 Hits
void DMP::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi)
{
    // Called on L2 hits - data already in L2
    if (!pfi.hasData()) {
        return;  // Safety check
    }
    
    // Data available from L2, scan for pointers
    Queued::notify(acc, pfi);
}
notifyFill()
 - Handles L2 Fills
void DMP::notifyFill(const CacheAccessProbeArg &acc)
{
    const PacketPtr &pkt = acc.pkt;
    
    // Called after L2 fills from memory/L3
    if (!pkt->isRead() || pkt->cmd.isHWPrefetch()) {
        return;
    }
    
    // Create PrefetchInfo with filled data
    PrefetchInfo pfi(pkt, pkt->getAddr(), false);
    
    // Data available from fill, scan for pointers
    Queued::notify(acc, pfi);
}```

Key Points

```
Both paths have data available - that's the critical requirement
L2 hits: Data already in cache → 
notify()
L2 fills: Data just fetched → 
notifyFill()
Both call Queued::notify() which invokes 
calculatePrefetch()
calculatePrefetch()
 has safety check - if (!pfi.hasData()) return;
```

Testing
After rebuild:

make debug-ptrchase
Expected output for L2 hits:

DMP::notify called for addr 0x... (hasData: 1)
DMP: Data available, proceeding with pointer scan
Expected output for L2 fills:

DMP::notifyFill called for addr 0x... (hasData: 1)
DMP: Data available, proceeding with pointer scan
Both paths should generate prefetches!

Issues:

after adding more filters in the notify function, enforcing using virtual address, and plugging in an MMU (also force accessing the cache using virtual address), no prefetch requests are issued anymore...
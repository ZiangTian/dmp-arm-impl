// src/mem/cache/prefetch/dmp.cc
#include "mem/cache/prefetch/dmp.hh"

#include "debug/HWPrefetch.hh"
#include "params/DMPPrefetcher.hh" // Auto-generated header for params
#include "mem/cache/cache_blk.hh"
#include "sim/system.hh"
#include "sim/cur_tick.hh"


/**
 * Other todos:
 * - Modify Gem5 native caches
 *   - Add 'dmpNoScan' flag to CacheBlk? or define it in DMP class?
 *   - gem5 L2 cache blk size is the same as L1's , and handle mismatch at the L1/L2 interface (Ruby??)
 *   - cache coherency effects: apple's caches is strictly inclusive; every single cacheline present in L1 must be in L2
 *     - L1 misses, data copied from L2 to L1: 
 *       - if this is the first time the 64B gets copied, trigger DMP scan, and then place the NOSCAN hint on L1 cacheline
 *     - L1 misses, L2 also misses, data copied from memory to both L2 and L1
 * - History filter
 *   - directly mapped, with 128/256 entries
 *   - LRU replacement?
 * - calculatePrefetch args
 *   - Make sure of the information passed to calculatePrefetch (isCacheMiss, hasData, getSize, etc)
 * - find out where the data gets put in the cache
 * - find out what happens if the wrong data gets prefetched, whether the LSQ will preempt it.
 */

namespace gem5
{
namespace prefetch
{

// Constructor: Pass params to parent 'Queued' class and initialize your members
DMP::DMP(const DMPPrefetcherParams &p)
    : Queued(p),
        chasePointers(p.chase_pointers),
        historyTable(p.history_table_entries, 0),
        historyTableSize(p.history_table_entries),
        byteOrder(p.sys->getGuestByteOrder())
{
    DPRINTF(HWPrefetch, "DMP Prefetcher initialized!\n");
    DPRINTF(HWPrefetch, "  chase_pointers: %d\n", chasePointers);
    DPRINTF(HWPrefetch, "  history_table_entries: %d\n", historyTableSize);
    DPRINTF(HWPrefetch, "  block_size: %d\n", blkSize);
    DPRINTF(HWPrefetch, "  use_virtual_addresses: %d\n", useVirtualAddresses);
}

bool DMP::inHistoryTable(Addr target_addr) {
    // direct mapping hash
    unsigned idx = (target_addr / blkSize) % historyTableSize;
    if (historyTable[idx] == target_addr) {
        return true;
    } else {
        historyTable[idx] = target_addr;
        return false;   
    }
}

void DMP::checkResetHistoryTable() {
    if (curTick() - lastPrefetchTick > resetThreshold) {
        DPRINTF(HWPrefetch, "DMP: Resetting history table due to inactivity\n");
        std::fill(historyTable.begin(), historyTable.end(), 0);
    }
    lastPrefetchTick = curTick(); // update here or at the end of calculatePrefetch?
}

// TODO: in notify and notifyFill, pass whole cache block hit or prefetched into the pfi 

void
DMP::notifyFill(const CacheAccessProbeArg &acc)
{   
    checkResetHistoryTable();

    const PacketPtr &pkt = acc.pkt;
    const CacheAccessor &cache = acc.cache;

    // if (pkt->cmd.isHWPrefetch() ||
    //     pkt->cmd == MemCmd::HardPFReq || 
    //     pkt->cmd == MemCmd::HardPFResp ||
    //     pkt->req->isPrefetch()) {
    //     DPRINTF(HWPrefetch, "DMP::notifyFill skipping HW prefetch packet (cmd: %s)\n",
    //             pkt->cmd.toString());
    //     return;
    // }

    if (pkt->req->taskId() == context_switch_task_id::Prefetcher) {
        DPRINTF(HWPrefetch, "DMP::notifyFill skipping prefetch (taskId=Prefetcher)\n");
        return;
    }
    
    if (pkt->cmd.isSWPrefetch()) return;
    // if (pkt->cmd.isHWPrefetch()) return;
    if (pkt->req->isCacheMaintenance()) return;
    if (pkt->isCleanEviction()) return; // evictions have no new data
    if (!pkt->req->hasPC()) return; // non-demand accesses have no PC
    if (pkt->req->isUncacheable()) return; // skip uncached accesses
    if (pkt->req->isInstFetch() && !onInst) return; // skip instruction fetches if not enabled
    if (!pkt->req->hasPC()) return; // skip if no PC (non-demand)
    if (!pkt->hasData()) {
        DPRINTF(HWPrefetch, "ERROR: Filled but has no data\n");
        abort();
    }

    // DPRINTF(HWPrefetch, "DMP::notifyFill for demand miss at addr %#x\n", 
    //         fill_addr);
    
    Addr addr;
    if (useVirtualAddresses && pkt->req->hasVaddr()) {
        addr = pkt->req->getVaddr();
        DPRINTF(HWPrefetch, "DMP::notifyFill using virtual address %#x\n", 
                addr);

        PrefetchInfo pfi(pkt, addr, false);  // miss=false because this is a fill. TODO: debatable

        // assert(pkt->hasData() && pfi.hasData());
        Queued::notify(acc, pfi);
    } else {
        DPRINTF(HWPrefetch, "DMP::notify WARNING: demand request without VA? cmd: %s taskId: %d\n",
                pkt->cmd.toString(), pkt->req->taskId());
        return;
    }
}

void
DMP::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi)
{   
    checkResetHistoryTable();

    DPRINTF(HWPrefetch, "DMP::notify called for addr %#x (hasData: %d)\n", 
            pfi.getAddr(), pfi.hasData());
    
    const PacketPtr &pkt = acc.pkt;
    const CacheAccessor &cache = acc.cache;

    // if (pkt->cmd.isHWPrefetch() ||
    //     pkt->cmd == MemCmd::HardPFReq || 
    //     pkt->cmd == MemCmd::HardPFResp ||
    //     pkt->req->isPrefetch()) {
    //     DPRINTF(HWPrefetch, "DMP::notifyFill skipping HW prefetch packet (cmd: %s)\n",
    //             pkt->cmd.toString());
    //     return;
    // }
    if (pkt->req->taskId() == context_switch_task_id::Prefetcher) {
        DPRINTF(HWPrefetch, "DMP::notify skipping prefetch (taskId=Prefetcher)\n");
        return;
    }

    // if(pkt->cmd.isHWPrefetch()) return;

    // only look at L2 hits, because misses won't have data to scan yet; we delegate fills to notifyFill()
    if (pfi.isCacheMiss()) {
        DPRINTF(HWPrefetch, "DMP::notify skipping (L2 cache miss)\n");
        // confirm that data is not available on L2 miss
        if (pfi.hasData()) {
            DPRINTF(HWPrefetch, "DMP::notify WARNING: L2 miss but hasData=true\n");
            abort();
        }
        return;
    }

    // use virtual addresses
    if (useVirtualAddresses && acc.pkt->req->hasVaddr()) {
        DPRINTF(HWPrefetch, "DMP::notify using virtual address %#x\n", 
                acc.pkt->req->getVaddr());
        PrefetchInfo pfi_va(acc.pkt, acc.pkt->req->getVaddr(), false);
        Queued::notify(acc, pfi_va);
    } else {
        DPRINTF(HWPrefetch, "DMP::notify WARNING: demand request without VA? cmd: %s taskId: %d\n",
                pkt->cmd.toString(), pkt->req->taskId());
        // Queued::notify(acc, pfi);
        // abort(); 
        return;
    }
}

void
DMP::calculatePrefetch(const PrefetchInfo &pfi,
                       std::vector<AddrPriority> &addresses,
                       const CacheAccessor &cache)
{
    DPRINTF(HWPrefetch, "\n=== DMP::calculatePrefetch ENTRY ===\n");
    DPRINTF(HWPrefetch, "  Address: %#x\n", pfi.getAddr());
    DPRINTF(HWPrefetch, "  isWrite: %d\n", pfi.isWrite());
    DPRINTF(HWPrefetch, "  Size: %d\n", pfi.getSize());
    
    // by the time this is called, data should be available
    if (!pfi.hasData()) {
        DPRINTF(HWPrefetch, "ERROR: No data available\n");
        return;
        // abort();
    }

    // by the time this is called, we shouldn't deal with non-demand accesses
    if (!pfi.hasPC()) {
        DPRINTF(HWPrefetch, "ERROR: Not demand\n");
        abort();
    }

    // if pointer chasing is disabled, skip
    if (!chasePointers) {
        return;
    }

    // Addr line_addr = blockAddress(pfi.getAddr());
    Addr phys_addr = pfi.getPaddr();
    Addr line_addr = blockAddress(phys_addr);
    Addr virt_addr = pfi.getAddr();
    bool is_secure = pfi.isSecure();
    
    // print debug: physical address, block address, virtual address if available, and just address
    DPRINTF(HWPrefetch, "DMP: Physical address: %#x\n", phys_addr);
    DPRINTF(HWPrefetch, "DMP: Block address: %#x\n", line_addr);
    DPRINTF(HWPrefetch, "DMP: Virtual address: %#x\n", virt_addr);
    

    DPRINTF(HWPrefetch, "DMP: Processing cache line at %#x (secure: %d)\n", 
            line_addr, is_secure);

    // Cache is physically indexed, using the line address
    const CacheBlk *blk = cache.getCacheLine(line_addr, is_secure);
    if (!blk || !blk->isValid()) {
        DPRINTF(HWPrefetch, "DMP: Cache line %#x not found or invalid, skipping\n",
                line_addr);
        return;
    }
    
    // Check if this cacheline has the no-scan flag set
    if (blk->dmpNoScan) {
        DPRINTF(HWPrefetch, "DMP: Cache line %#x marked no-scan, skipping\n",
                line_addr);
        return;
    }
    
    // Access the cache block data directly
    const uint8_t *block_data = blk->data;
    if (!block_data) {
        DPRINTF(HWPrefetch, "DMP: Cache block has no data, skipping\n");
        return;
    }

    // // preview the entire data block in hex
    // DPRINTF(HWPrefetch, "DMP: Cache block data preview:\n");
    // for (unsigned offset = 0; offset < blkSize; offset += 16) {
    //     DPRINTF(HWPrefetch, "  %04x: ", offset);
    //     for (unsigned i = 0; i < 16 && (offset + i) < blkSize; i++) {
    //         DPRINTF(HWPrefetch, "%02x ", block_data[offset + i]);
    //     }
    //     DPRINTF(HWPrefetch, "\n");
    // }

    // Scan each 64-bit aligned chunk in the ENTIRE cache line
    // blkSize is typically 64 bytes, giving us 8 candidate pointers
    const unsigned num_candidates = blkSize / sizeof(uint64_t);
    // DPRINTF(HWPrefetch, "DMP: Scanning %d candidate pointers (blkSize=%d)\n",
    //         num_candidates, blkSize);
    
    for (unsigned i = 0; i < num_candidates; i++) {
        // DPRINTF(HWPrefetch, "DMP: Examining candidate %d/%d at offset %d\n", 
        //         i+1, num_candidates, i * sizeof(uint64_t));
        
        // Extract 64-bit candidate pointer from the cache block data
        // Read from the appropriate offset in the cache line
        uint64_t candidate_ptr;
        const uint64_t *data_ptr = reinterpret_cast<const uint64_t*>(block_data + i * sizeof(uint64_t));
        
        // Apply byte order conversion
        if (byteOrder == ByteOrder::little) {
            candidate_ptr = letoh(*data_ptr);
        } else {
            candidate_ptr = betoh(*data_ptr);
        }
        
        DPRINTF(HWPrefetch, "DMP: Raw candidate value: %#llx\n", candidate_ptr);
        
        // Apply Top-Byte-Ignore (ARMv8-A): mask out top 8 bits
        candidate_ptr = candidate_ptr & 0x00FFFFFFFFFFFFFFULL;
        DPRINTF(HWPrefetch, "DMP: After TBI mask: %#llx\n", candidate_ptr);

        /* we can also consider adding a stricter check here:
        ** After TBI mask and before 4GB check, add range check:
        **  if (candidate_ptr < 0x400000) {  // Below typical code/data start
        **      DPRINTF(HWPrefetch, "DMP: Candidate %#llx too low, likely not pointer\n",
        **              candidate_ptr);
        **      continue;
        **  }
        */
        
        // 4GB Region Rule: Check if upper 32 bits match the current access
        // This filters out invalid pointers
        uint64_t current_upper = (virt_addr >> 32) & 0xFFFFFFFF;
        uint64_t candidate_upper = (candidate_ptr >> 32) & 0xFFFFFFFF;
        
        // DPRINTF(HWPrefetch, "DMP: 4GB check - current_upper: %#x, candidate_upper: %#x\n",
        //         current_upper, candidate_upper);
        
        if (current_upper != candidate_upper) {
            DPRINTF(HWPrefetch, "DMP: Candidate %#llx FAILED 4GB region check\n",
                    candidate_ptr);
            continue;
        }
        DPRINTF(HWPrefetch, "DMP: Candidate %#llx PASSED 4GB region check\n",
                candidate_ptr);
        
        // Check history filter to avoid redundant prefetches
        Addr target_block = blockAddress(candidate_ptr);
        // DPRINTF(HWPrefetch, "DMP: Target block address: %#x\n", target_block);
        
        if (inHistoryTable(target_block)) {
            DPRINTF(HWPrefetch, "DMP: Target %#x ALREADY in history, skipping\n",
                    target_block);
            continue;
        }
        
        DPRINTF(HWPrefetch, "DMP: Target %#x NOT in history, generating prefetches\n",
                target_block);
        
        // Target prefetch: issue prefetch for the L2 cacheline
        addresses.push_back(AddrPriority(target_block, 0));
        DPRINTF(HWPrefetch, "DMP: *** GENERATED target prefetch for %#x ***\n",
                target_block);
        
        // Auxiliary next-line prefetch: fetch the adjacent cache line
        Addr next_line = target_block + blkSize;
        // add next line to history filter
        if (!inHistoryTable(next_line)) {
            addresses.push_back(AddrPriority(next_line, 0));
        }

        DPRINTF(HWPrefetch, "DMP: *** GENERATED auxiliary prefetch for %#x ***\n",
                next_line);
    }
    
    CacheBlk *mutable_blk = const_cast<CacheBlk*>(blk);
    mutable_blk->setDmpNoScan();
    
    DPRINTF(HWPrefetch, "DMP: Total prefetches generated: %d\n", addresses.size());
    DPRINTF(HWPrefetch, "=== DMP::calculatePrefetch EXIT ===\n\n");
}

} // namespace prefetch
} // namespace gem5

# Data Memory Dependent Prefetcher Impl

## Quick test run

```
cd mybuild
mkdir -p mybuildout
make debug-umov > umov.log 2>&1
```

`umov.log` logs details for every prefetcher event, blocks accessed, and pointer candidates.

## Test program

```
cd mybuild
nohup make test-no-dmp-ptrchase > db.log 2>&1 &
nohup make test-ptrchase > db_dmp.log 2>&1 &
```

## Key implementation

Main implementation is in  `src/mem/cache/prefetch/dmp.hh` and `src/mem/cache/prefetch/dmp.cc`. One key strategy is, to activate DMP on L1 cache miss and prefetch into L2, we design DMP to activate on any L2 access. This includes L2 hit and L2 miss. Since we need to have the requested block to scan when activating the prefetcher, we have two notify functions that trigger the activation: `notify()` and `notifyFill()`. `notify()` is only called on L2 hits; `notifyFill()` is called only on L2 fills (after the data missed in L2 is serviced from the DRAM).

`configs/common/CacheConfig.py` contains parameters for caches that can be tweaked. An MMU is plugged in here to solve the cross-page prefetch bug.

For more internals, `src/mem/cache/base.cc`, `src/mem/cache/prefetch/base.cc`, and `src/mem/cache/prefetch/queued.cc` are recommended to check out. 







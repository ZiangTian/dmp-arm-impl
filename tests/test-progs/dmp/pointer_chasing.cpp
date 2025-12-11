#include <cstdint>
#include <vector>
#include <iostream>

int main() {
    // contiguous memory for pointers and their targets.
    // keep addresses close (satisfying the DMP 4GB rule).
    const int N = 1024; 
    std::vector<uint64_t> memory(N * 2); 

    // split into two logical regions
    uint64_t* pointers = &memory[0];      // The "Array of Pointers" (AoP)
    uint64_t* targets  = &memory[N];      // The data they point to

    // initialize: Fill 'pointers' with the addresses of 'targets'
    for (int i = 0; i < N; i++) {
        // pointers[i] holds the address of targets[i]
        pointers[i] = (uint64_t)&targets[i]; 
    }

    // trigger: Access the pointers (L1 Fills)
    // read the pointers without dereferencing them 
    volatile uint64_t sink; 
    for (int i = 0; i < N; i++) {
        // 1. Load the pointer (Trigger DMP)
        uint64_t* target_addr = (uint64_t*)pointers[i];
        
        // 2. Access the data (Benefit from DMP)
        // This will Stall the CPU if data is not in cache.
        // If DMP works, this stall is reduced/eliminated.
        sink = *target_addr;
    }

    std::cout << "Sink: " << sink << std::endl;

    return 0;
}
#include <cstdint>
#include <vector>
#include <iostream>

int main() {
    const int N = 1024; 
    std::vector<uint64_t> memory(N * 2); 

    // split into two logical regions
    uint64_t* pointers = &memory[0];      // The "Array of Pointers" (AoP)
    uint64_t* targets  = &memory[N];      // The data they point to

    for (int i = 0; i < N; i++) {
        pointers[i] = (uint64_t)&targets[i]; 
    }

    volatile uint64_t sink; 
    for (int i = 0; i < N; i++) {
        uint64_t* target_addr = (uint64_t*)pointers[i];
        
        sink = *target_addr;
    }

    std::cout << "Sink: " << sink << std::endl;

    return 0;
}
#include <cstdint>
#include <vector>
#include <iostream>
#include <algorithm> // For std::random_shuffle if needed
#include <random>

const int N = 1024 * 1024; 

void trash_cache() {
    // 32MB garbage buffer
    const int GARBAGE_SIZE = 1024 * 1024 * 32; 
    std::vector<char> garbage(GARBAGE_SIZE);
    
    for (int i = 0; i < GARBAGE_SIZE; i++) {
        garbage[i] = (char)i;
    }
    std::cout << "Cache trashed." << std::endl;
}

int main() {
    std::cout << "Allocating " << (N * 2 * 8) / (1024*1024) << "MB of memory..." << std::endl;
    std::vector<uint64_t> memory(N * 2); 

    uint64_t* pointers = &memory[0];      
    uint64_t* targets  = &memory[N];      

    // link sequentially
    for (int i = 0; i < N; i++) {
        pointers[i] = (uint64_t)&targets[i]; 
    }
    std::cout << "Initialization complete." << std::endl;

    trash_cache();

    std::cout << "Starting measurement..." << std::endl;
    
    volatile uint64_t sink; 
    for (int i = 0; i < 3; i++) {
        uint64_t* target_addr = (uint64_t*)pointers[i];
    }

    for (int i = 3; i < N; i++) {
        uint64_t* target_addr = (uint64_t*)pointers[i];
        
        // Dereference (DMP should have brought this into L2 already!)
        sink = *(uint64_t*)pointers[i - 3];
    }

    for (int i = N - 3; i < N; i++) {
        sink = *(uint64_t*)pointers[i];
    }

    std::cout << "Sink: " << sink << std::endl;
    return 0;
}
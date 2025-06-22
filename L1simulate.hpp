#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 'inline' allows these variables to be defined in a header and included in multiple translation units without linker errors.
inline std::string tracefile = "";
inline std::string outfilename = "";
inline int s = -1;
inline int E = -1;
inline int b = -1;
inline uint64_t bus_cycles = 0;
inline uint64_t memory_cycles = 0;
inline uint64_t block_size = 0;

// Number of processor cores (change this to scale the simulation)
inline int NUM_CORES = 4;

// Per-core data
inline std::vector<size_t> pc(NUM_CORES, 0);
inline std::vector<bool> core_done(NUM_CORES, false);
inline std::vector<uint64_t> core_wait_until(NUM_CORES, 0);

inline uint64_t global_cycle = 0;
inline size_t active_cores = NUM_CORES;

// MESI protocol states
enum class MESIState { INVALID, EXCLUSIVE, SHARED, MODIFIED };

// Cache line structure
struct CacheLine {
    uint32_t tag = 0;
    MESIState mesi = MESIState::INVALID;
    uint64_t lru_counter = 0;
};

// Cache statistics structure
struct CacheStats {
    uint64_t total_instructions = 0;
    uint64_t total_reads = 0;
    uint64_t total_writes = 0;
    uint64_t total_cycles = 0;
    uint64_t idle_cycles = 0;
    uint64_t cache_misses = 0;
    uint64_t cache_evictions = 0;
    uint64_t writebacks = 0;
    uint64_t bus_invalidations = 0;
    uint64_t data_traffic_bytes = 0;
};

// L1 Cache structure for a single core
struct L1Cache {
    int S; // Number of sets
    int E; // Associativity
    int B; // Block size in bytes
    uint64_t global_lru_counter = 0; // For LRU tracking

    // 2D vector: sets[set_idx][line_idx]
    std::vector<std::vector<CacheLine>> sets;

    CacheStats stats;

    L1Cache(int s_bits, int E, int b_bits)
        : S(1 << s_bits), E(E), B(1 << b_bits), sets(S, std::vector<CacheLine>(E, CacheLine())) {}

    int find_line(const std::vector<CacheLine>& set, uint32_t tag) const {
        for (size_t i = 0; i < set.size(); ++i) {
            if (set[i].tag == tag) return static_cast<int>(i);
        }
        return -1;
    }
    int find_lru(const std::vector<CacheLine>& set) const {
        uint64_t min_lru = UINT64_MAX;
        int lru_idx = 0;
        for (size_t i = 0; i < set.size(); ++i) {
            if (set[i].mesi == MESIState::INVALID) return static_cast<int>(i); // Prefer invalid
            if (set[i].lru_counter < min_lru) {
                min_lru = set[i].lru_counter;
                lru_idx = static_cast<int>(i);
            }
        }
        return lru_idx;
    }
};

// Bus transaction/request types for coherence
enum class BusRequestType {
    BUSRD,    // Bus Read: A core requests to read a cache block (for a read miss)
    BUSRDX,   // Bus Read-Exclusive: A core requests to read and then write a cache block (for a write miss, intent to modify)
    BUSUPGR,  // Bus Upgrade: A core requests to upgrade its shared copy to exclusive/modified (for a write hit on a shared line)
    FLUSH,     // Flush: A core writes back a modified block to memory (usually on eviction)
};

struct Bus {
    int src_core;
    uint32_t addr;
    BusRequestType req_type;
    uint64_t cycles_remaining;
    int resp_core;
    bool available = true;
    bool done = true;
    int prev_core;
    BusRequestType prev_req_type;
    MESIState prev_mesi_state;
    bool evict;
};

// Cache controller for coherence
class CacheController {
public:
    std::vector<L1Cache> l1_caches;
    uint64_t total_bus_transactions = 0;
    uint64_t total_bus_traffic_bytes = 0;

    CacheController(int num_cores, int s_bits, int E, int b_bits)
        : l1_caches(num_cores, L1Cache(s_bits, E, b_bits)) {}

    // Simulate a memory reference for a core
    void process_memory_access(int core_id, uint32_t addr, bool is_write, Bus& bus);
    
    void mesi_snoop(Bus& bus);
};

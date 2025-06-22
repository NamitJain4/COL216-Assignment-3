#include "L1simulate.hpp"

struct TraceEntry {
    char op; // 'R' or 'W'
    uint32_t addr;
};

std::vector<TraceEntry> read_trace(const std::string& filename) {
    std::vector<TraceEntry> trace;
    std::ifstream fin(filename);
    std::string line;
    while (std::getline(fin, line)) {
        std::istringstream iss(line);
        char op;
        std::string addr_str;
        if (!(iss >> op >> addr_str)) continue;
        uint32_t addr = std::stoul(addr_str, nullptr, 16);
        trace.push_back({op, addr});
    }
    return trace;
}

void print_help() {
    std::cout << "Usage: ./L1simulate -t <tracefile> -s <s> -E <E> -b <b> -o <outfilename> [-h]\n";
    std::cout << "  -t <tracefile>   : name of parallel application (e.g. app1)\n";
    std::cout << "  -s <s>           : number of set index bits (number of sets = 2^s)\n";
    std::cout << "  -E <E>           : associativity (number of cache lines per set)\n";
    std::cout << "  -b <b>           : number of block bits (block size = 2^b)\n";
    std::cout << "  -o <outfilename> : output log file\n";
    std::cout << "  -h               : print this help message\n";
}

void print_stats(const std::vector<L1Cache>& caches, uint64_t total_bus_transactions, uint64_t total_bus_traffic_bytes) {
    std::cout << "Simulation Parameters:\n";
    std::cout << "Trace Prefix: " << tracefile << "\n";
    std::cout << "Set Index Bits: " << s << "\n";
    std::cout << "Associativity: " << E << "\n";
    std::cout << "Block Bits: " << b << "\n";
    std::cout << "Block Size (Bytes): " << (1 << b) << "\n";
    std::cout << "Number of Sets: " << (1 << s) << "\n";
    std::cout << "Cache Size (KB per core): " << ((1 << s) * E * (1 << b)) / 1024 << "\n";
    std::cout << "MESI Protocol: Enabled\n";
    std::cout << "Write Policy: Write-back, Write-allocate\n";
    std::cout << "Replacement Policy: LRU\n";
    std::cout << "Bus: Central snooping bus\n\n";

    for (int i = 0; i < NUM_CORES; ++i) {
        const auto& stats = caches[i].stats;
        std::cout << "Core " << i << " Statistics:\n";
        std::cout << "Total Instructions: " << stats.total_instructions << "\n";
        std::cout << "Total Reads: " << stats.total_reads << "\n";
        std::cout << "Total Writes: " << stats.total_writes << "\n";
        std::cout << "Total Execution Cycles: " << stats.total_cycles << "\n";
        std::cout << "Idle Cycles: " << stats.idle_cycles << "\n";
        std::cout << "Cache Misses: " << stats.cache_misses << "\n";
        double miss_rate = stats.total_instructions ? (100.0 * stats.cache_misses / stats.total_instructions) : 0.0;
        std::cout << "Cache Miss Rate: " << std::fixed << std::setprecision(2) << miss_rate << "%\n";
        std::cout << "Cache Evictions: " << stats.cache_evictions << "\n";
        std::cout << "Writebacks: " << stats.writebacks << "\n";
        std::cout << "Bus Invalidations: " << stats.bus_invalidations << "\n";
        std::cout << "Data Traffic (Bytes): " << stats.data_traffic_bytes << "\n\n";
    }
    std::cout << "Overall Bus Summary:\n";
    std::cout << "Total Bus Transactions: " << total_bus_transactions << "\n";
    std::cout << "Total Bus Traffic (Bytes): " << total_bus_traffic_bytes << "\n";
}

// Utility: extract tag and set index from address
uint32_t get_tag(uint32_t addr) {
    return addr >> (b + s);
}
uint32_t get_set_index(uint32_t addr) {
    return (addr >> b) & ((1 << s) - 1);
}

// CacheController::process_memory_access implementation
void CacheController::process_memory_access(int core_id, uint32_t addr, bool is_write, Bus& bus) {
    L1Cache& cache = l1_caches[core_id];
    uint32_t tag = get_tag(addr);
    uint32_t set_idx = get_set_index(addr);
    auto& set = cache.sets[set_idx];
    int idx = cache.find_line(set, tag);

    // Hit
    if (idx != -1 && set[idx].mesi != MESIState::INVALID) {
        if (is_write) {
            if (set[idx].mesi == MESIState::SHARED) {
                if (bus.available) {
                    bus.src_core = core_id;
                    bus.addr = addr;
                    bus.req_type = BusRequestType::BUSUPGR;
                    bus.available = false;

                    cache.stats.bus_invalidations++;
                    total_bus_transactions++;
                } else {
                    if (core_id == bus.src_core) {
                        cache.stats.total_cycles++;
                    } else {
                        cache.stats.idle_cycles++;
                    }
                    return;
                }
            }
            // Write the value
            set[idx].mesi = MESIState::MODIFIED;
            set[idx].lru_counter = ++cache.global_lru_counter;
            cache.stats.total_writes++;
        } else {
            // Read the value
            set[idx].lru_counter = ++cache.global_lru_counter;
            cache.stats.total_reads++;
        }
        cache.stats.total_instructions++;
        cache.stats.total_cycles++;
        pc[core_id]++;
        return;
    }

    // Miss: need to handle MESI protocol and bus
    if (is_write) {
        if (bus.available) {
            bus.src_core = core_id;
            bus.addr = addr;
            bus.req_type = BusRequestType::BUSRDX;
            bus.available = false;
            bus.done = true;

            cache.stats.total_cycles++;
            cache.stats.cache_misses++;
            cache.stats.bus_invalidations++;
            cache.stats.data_traffic_bytes += block_size;
            total_bus_transactions++;
            total_bus_traffic_bytes += block_size;
        } else {
            if (core_id == bus.src_core) {
                cache.stats.total_cycles++;
            } else {
                cache.stats.idle_cycles++;
            }
            return;
        }
    } else {
        if (bus.available) {
            bus.src_core = core_id;
            bus.addr = addr;
            bus.req_type = BusRequestType::BUSRD;
            bus.available = false;
            bus.done = true;

            cache.stats.total_cycles++;
            cache.stats.cache_misses++;
            cache.stats.data_traffic_bytes += block_size;
            total_bus_transactions++;
            total_bus_traffic_bytes += block_size;
        } else {
            if (core_id == bus.src_core) {
                cache.stats.total_cycles++;
            } else {
                cache.stats.idle_cycles++;
            }
            return;
        }
    }
}

// MESI snoop: update other caches on bus transaction
void CacheController::mesi_snoop(Bus& bus) {
    if (bus.available) return;
    bool cache_responded = false;

    uint32_t tag = get_tag(bus.addr);
    uint32_t set_idx = get_set_index(bus.addr);
    std::vector<int> sharers;
    if (bus.req_type == BusRequestType::BUSRD || bus.req_type == BusRequestType::BUSRDX) {
        auto& src_set = l1_caches[bus.src_core].sets[set_idx];
        int src_idx = l1_caches[bus.src_core].find_lru(src_set);
        if (src_set[src_idx].mesi != MESIState::INVALID) {
            for (int core = 0; core < NUM_CORES; ++core) {
                if (core == bus.src_core) continue;
                auto& set = l1_caches[core].sets[set_idx];
                int idx = l1_caches[core].find_line(set, tag);
                if (idx != -1 && set[idx].mesi == MESIState::SHARED) {
                    sharers.push_back(core);
                }
            }
            if (sharers.size() == 1) {
                auto& set = l1_caches[sharers[0]].sets[set_idx];
                int idx = l1_caches[sharers[0]].find_line(set, tag);
                set[idx].mesi = MESIState::EXCLUSIVE;
            }
            if (src_set[src_idx].mesi == MESIState::MODIFIED) {
                bus.prev_req_type = bus.req_type;
                bus.req_type = BusRequestType::FLUSH;
                bus.evict = true;
                l1_caches[bus.src_core].stats.writebacks++;
                l1_caches[bus.src_core].stats.data_traffic_bytes += block_size;
                total_bus_transactions++;
                total_bus_traffic_bytes += block_size;
            }
            src_set[src_idx].mesi = MESIState::INVALID;
            l1_caches[bus.src_core].stats.cache_evictions++;
        }
    }

    if (bus.req_type == BusRequestType::FLUSH && bus.evict) {
        if (bus.done) {
            bus.cycles_remaining = memory_cycles;
            bus.resp_core = -1;
            bus.done = false;
        }
        if (!bus.cycles_remaining) {
            bus.req_type = bus.prev_req_type;
            bus.done = true;
        }
    }

    // Iterating through each core
    for (int core = 0; core < NUM_CORES; ++core) {
        auto& set = l1_caches[core].sets[set_idx];
        int idx = l1_caches[core].find_line(set, tag);

        if (core == bus.src_core) continue;
        if (idx == -1) continue;
        if (set[idx].mesi == MESIState::INVALID) continue;

        if (bus.req_type == BusRequestType::BUSRD) {
            if (bus.done) {
                bus.cycles_remaining = bus_cycles;
                bus.resp_core = core;
                bus.done = false;
                bus.prev_mesi_state = set[idx].mesi;
                set[idx].mesi = MESIState::SHARED;
            }
            if (core == bus.resp_core && !bus.cycles_remaining) {
                bus.available = true;
                bus.done = true;
                set[idx].lru_counter = ++l1_caches[core].global_lru_counter;
                l1_caches[core].stats.data_traffic_bytes += block_size;

                auto& set = l1_caches[bus.src_core].sets[set_idx];
                int idx = l1_caches[bus.src_core].find_lru(set);
                set[idx].mesi = MESIState::SHARED;
                set[idx].tag = tag;

                if (bus.prev_mesi_state == MESIState::MODIFIED) {
                    bus.prev_req_type = bus.req_type;
                    bus.src_core = core;
                    bus.req_type = BusRequestType::FLUSH;
                    bus.available = false;
                    bus.evict = false;
                    l1_caches[core].stats.writebacks++;
                    l1_caches[core].stats.data_traffic_bytes += block_size;
                    total_bus_transactions++;
                    total_bus_traffic_bytes += block_size;
                }
            }
            cache_responded = true;
        }
        if (bus.req_type == BusRequestType::BUSRDX) {
            if (set[idx].mesi == MESIState::MODIFIED) {
                bus.prev_core = bus.src_core;
                bus.prev_req_type = bus.req_type;
                bus.src_core = core;
                bus.req_type = BusRequestType::FLUSH;
                bus.evict = false;
                l1_caches[bus.prev_core].stats.total_cycles--;
                l1_caches[bus.prev_core].stats.idle_cycles++;
                l1_caches[core].stats.writebacks++;
                l1_caches[core].stats.data_traffic_bytes += block_size;
                total_bus_transactions++;
                total_bus_traffic_bytes += block_size;
            }
            set[idx].mesi = MESIState::INVALID;
        }
        if (bus.req_type == BusRequestType::BUSUPGR) {
            if (set[idx].mesi == MESIState::SHARED) {
                set[idx].mesi = MESIState::INVALID;
            }
        }
    }

    if (bus.req_type == BusRequestType::BUSUPGR) {
        bus.available = true;
    }

    // Memory response
    if (bus.req_type == BusRequestType::FLUSH && !bus.evict) {
        if (bus.done) {
            bus.cycles_remaining = memory_cycles;
            bus.resp_core = -1;
            bus.done = false;
        }
        if (!bus.cycles_remaining) {
            if (bus.prev_req_type == BusRequestType::BUSRDX) {
                bus.src_core = bus.prev_core;
                bus.req_type = bus.prev_req_type;
                bus.done = true;
            } else {
                bus.available = true;
                bus.done = true;
            }
        }
    }
    if ((bus.req_type == BusRequestType::BUSRD && !cache_responded) || bus.req_type == BusRequestType::BUSRDX) {
        if (bus.done) {
            bus.cycles_remaining = memory_cycles;
            bus.resp_core = -1;
            bus.done = false;
        }
        if (!bus.cycles_remaining) {
            bus.available = true;
            bus.done = true;

            auto& set = l1_caches[bus.src_core].sets[set_idx];
            int idx = l1_caches[bus.src_core].find_lru(set);
            set[idx].mesi = bus.req_type == BusRequestType::BUSRDX ? MESIState::MODIFIED : MESIState::EXCLUSIVE;
            set[idx].tag = tag;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        print_help();
        return 0;
    }

    // First pass: check for -h anywhere
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
    }

    bool t_set = false, s_set = false, E_set = false, b_set = false, o_set = false;

    // Second pass: parse all other arguments robustly
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                tracefile = argv[++i];
                t_set = true;
            } else {
                std::cerr << "Error: -t requires a value.\n";
                print_help();
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                s = atoi(argv[++i]);
                s_set = true;
            } else {
                std::cerr << "Error: -s requires a value.\n";
                print_help();
                return 1;
            }
        } else if (strcmp(argv[i], "-E") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                E = atoi(argv[++i]);
                E_set = true;
            } else {
                std::cerr << "Error: -E requires a value.\n";
                print_help();
                return 1;
            }
        } else if (strcmp(argv[i], "-b") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                b = atoi(argv[++i]);
                b_set = true;
            } else {
                std::cerr << "Error: -b requires a value.\n";
                print_help();
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                outfilename = argv[++i];
                o_set = true;
            } else {
                std::cerr << "Error: -o requires a value.\n";
                print_help();
                return 1;
            }
        } else if (argv[i][0] == '-') {
            std::cerr << "Unknown argument: " << argv[i] << std::endl;
            print_help();
            return 1;
        } else {
            std::cerr << "Unexpected value: " << argv[i] << std::endl;
            print_help();
            return 1;
        }
    }

    if (!(t_set && s_set && E_set && b_set && o_set)) {
        std::cerr << "Error: All arguments -t, -s, -E, -b, -o are required.\n";
        print_help();
        return 1;
    }

    // Prepare trace file names for all cores
    std::vector<std::string> tracefiles(NUM_CORES);
    for (int i = 0; i < NUM_CORES; ++i) {
        tracefiles[i] = tracefile + "_proc" + std::to_string(i) + ".trace";
    }

    // Read traces
    std::vector<std::vector<TraceEntry>> traces(NUM_CORES);
    for (int i = 0; i < NUM_CORES; ++i) {
        traces[i] = read_trace(tracefiles[i]);
    }

    // Set up controller and caches
    CacheController controller(NUM_CORES, s, E, b);
    bus_cycles = 2 * controller.l1_caches[0].B / 4; // Bus cycles based on block size
    memory_cycles = 100; // Memory cycles for DRAM access
    block_size = 1 << b; // Block size in bytes

    Bus bus;

    while (active_cores > 0) {
        controller.mesi_snoop(bus);

        for (int core = 0; core < NUM_CORES; ++core) {
            if (core_done[core]) continue;
            if (pc[core] >= traces[core].size()) {
                core_done[core] = true;
                active_cores--;
                continue;
            }
            const TraceEntry& entry = traces[core][pc[core]];
            bool is_write = (entry.op == 'W');

            // Simulate access using the controller's MESI protocol logic
            controller.process_memory_access(core, entry.addr, is_write, bus);
        }
        
        controller.mesi_snoop(bus);
        bus.cycles_remaining = std::max(bus.cycles_remaining - 1, static_cast<uint64_t>(0));
        // std::cout << pc[0] << " " << pc[1] << " " << pc[2] << " " << pc[3] << " " << "\n";
        global_cycle++;
    }

    // Output stats
    print_stats(controller.l1_caches, controller.total_bus_transactions, controller.total_bus_traffic_bytes);

    // Optionally, write to output file
    if (!outfilename.empty()) {
        std::ofstream fout(outfilename);
        std::streambuf* coutbuf = std::cout.rdbuf();
        std::cout.rdbuf(fout.rdbuf());
        print_stats(controller.l1_caches, controller.total_bus_transactions, controller.total_bus_traffic_bytes);
        std::cout.rdbuf(coutbuf);
    }

    return 0;
}

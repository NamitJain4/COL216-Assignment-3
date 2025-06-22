# COL216_Assignment_3
Assignment 3: Simulation of L1 Cache in C++ for Quad Core Processors, with Cache Coherence Support

## Authors
- Rishabh Narayanan (2023CS10577)
- Namit Jain (2023CS10483)

## Directory Structure

This repository contains the following files:
- `L1simulate.cpp`, `L1simulate.hpp`: Core simulation code for L1 cache with MESI coherence protocol
- `makefile`: Build commands for the simulation
- `report.tex`: LaTeX source for the project report
- `mermaid_flowchart.jpg`, `mermaid.mmd`: Flowchart for the simulation logic
- `a3_report.pdf`: Final compiled report
- `assumptions.pdf`, `cache_assignment.pdf`: Assignment documentation
- `sample_traces/`: Directory containing test cases

## Test Cases

The `sample_traces/` directory contains several test cases to demonstrate different cache behaviors:

1. **False Sharing Test** (`app_falsesharing_proc*.trace`): 
   - Demonstrates performance impact when multiple cores access different variables in the same cache line
   - Parameters: s=6, E=2, b=5

2. **LRU Policy Test** (`app_lru_proc*.trace`):
   - Verifies the correct implementation of Least Recently Used replacement policy
   - Parameters: s=0, E=3, b=2

3. **Associativity Test** (`app_assoc_proc*.trace`):
   - Shows the impact of different associativity values on performance
   - Parameters: Variable associativity (E)

4. **Write-Back Policy Test** (`app_wb_proc*.trace`):
   - Demonstrates the efficiency of write-back vs. write-through policies
   - Parameters: s=0, E=1, b=2

5. **Report Test** (`app_report_proc*.trace`):
   - Larger traces (8000 instructions per core) used for the performance analysis in the report
   - Parameters: Variable based on experiment

## Usage

### Build Instructions

```bash
# Compile the simulator
make

# Clean build artifacts
make clean
```

### Running Test Cases

To run the simulator with specific test cases and parameters:

```bash
# General format
./L1simulate -t <tracefile_prefix> -s <s> -E <E> -b <b> -o <outfile>

# Examples for specific test cases:

# False Sharing Test
./L1simulate -t app_falsesharing -s 6 -E 2 -b 5 -o falsesharing_out.log

# LRU Policy Test
./L1simulate -t app_lru -s 0 -E 3 -b 2 -o lru_out.log

# Associativity Test (4-way)
./L1simulate -t app_assoc -s 6 -E 4 -b 5 -o assoc_out.log

# Write-Back Policy Test
./L1simulate -t app_wb -s 0 -E 1 -b 2 -o wb_out.log

# Report Test with default parameters
./L1simulate -t app_report -s 6 -E 2 -b 5 -o report_out.log
```

### Parameters

- `-t`: Trace file prefix (simulator will look for files named `<prefix>_proc0.trace`, `<prefix>_proc1.trace`, etc.)
- `-s`: Number of set index bits (number of sets = 2^s)
- `-E`: Associativity (number of lines per set)
- `-b`: Number of block bits (block size = 2^b bytes)
- `-o`: Output log file name

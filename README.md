VeloxDB

VeloxDB is a lightweight, high-performance C++ Key-Value storage engine inspired by the LSM-Tree (Log-Structured Merge-Tree) architecture. It bridges the gap between fast in-memory operations and persistent disk storage using a background-flushing mechanism.

✨ Features

High Concurrency: Thread-safe architecture utilizing std::shared_mutex for efficient many-reader, one-writer access.

Asynchronous Persistence: Memory thresholds trigger background threads to flush "Immutable MemTables" to disk without blocking active writes.

Binary Serialization: Custom binary format optimized for minimal disk footprint and fast sequential I/O.

Modern C++20 Core: Leverages std::filesystem for path management, std::format for logging, and std::optional for safe data retrieval.

🏗 Architecture

MemTable: Writes are first committed to an in-memory unordered_map.

Snapshotting: When the MemTable hits a size threshold, it is swapped with a fresh table and sent to a background flush queue.

SSTables (Disk): Data is persisted as .bin files in the ./data directory.

Read Path: VeloxDB implements a hierarchical search:
  Level 0: Active MemTable (In-memory).
  Level 1: Disk-based .bin files (searched Newest ->  Oldest).
  
🚀 Getting Started

Prerequisites

Compiler: GCC 10+, Clang 10+, or MSVC 19.29+ (C++20 support required).
Build System: Make (recommended).

Installation & Build

# Clone the repository
git clone https://github.com/deepx4344/VeloxDB.git
cd VeloxDB

# Build using Make
make

# Or build manually with G++
g++ -std=c++20 src/main.cpp -o veloxdb

Usage
Run the executable to enter the interactive shell:

./veloxdb

Commands 

Command	Description	Example
put [key] [val]	Insert or update a key-value pair	put user_1 alice
get [key]	Retrieve a value by its key	get user_1
length	Count items currently in memory	length
q or e	Graceful shutdown (flushes memory to disk)

⚠️ Important Notes

Data Persistence: The engine automatically creates and manages a ./data folder. Ensure the process has write permissions for its execution path.

Search Complexity: Disk lookups are currently (O(N)) based on the number of files. Future updates will introduce Bloom Filters and Compaction to maintain (O(1)) or (O(log N)) performance.

Exit Protocol: Always use the q command to exit. This ensures the current active MemTable is flushed to disk before the process terminates.

📜 License

Distributed under the MIT License. See LICENSE for more information.

Author: notSatosh on X.com
Project Link: https://github.com/deepx4344/VeloxDB

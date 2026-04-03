# Raft-KV: Fault-Tolerant Distributed Key-Value Store

A highly available, distributed key-value store built in C++ on top of a from-scratch implementation of the **Raft Consensus Algorithm**.

This project implements the core Raft mechanisms (Leader Election, Log Replication, and Log Compaction/Snapshotting) to ensure data remains strictly consistent across a cluster of nodes, even in the face of network partitions and server crashes.

## Features

* **Leader Election:** Randomized election timeouts ensure a single leader is established seamlessly.
* **Log Replication:** Writes are appended to a replicated log and only committed to the state machine once a majority of nodes acknowledge receipt.
* **Log Compaction (Snapshotting):** Periodic snapshotting prevents unbounded log growth, serializing the state machine to disk to allow crashed nodes to recover quickly.
* **Fault Tolerance:** Withstands network partitions, leader crashes, and split-brain scenarios without compromising data integrity.
* **gRPC Communication:** Uses Protocol Buffers and gRPC for fast, typed, and reliable inter-node communication.

## Architecture

```text
┌──────────────────────────────┐
│  Client (CLI)                │  ← Sends GET/PUT/DEL requests
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────┐
│  KV Service (gRPC)           │  ← Routes requests to current Leader
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────┐
│  Raft Consensus Module       │  ← Manages Logs, State, and RPCs
└──────────────┬───────────────┘
               │
┌──────────────▼───────────────┐
│  KV Store (State Machine)    │  ← In-Memory Hash Map
└──────────────────────────────┘
```

## Performance Benchmarks

*Benchmarked on a local 3-node cluster handling 2,000 continuous `PUT` operations via a single-threaded client.*

* **Throughput:** `18` operations/second
* **Average Latency:** `53.65` ms/operation
* *Note: Throughput is currently bounded by the sequential, single-threaded nature of the CLI client. The ~53ms latency accounts for full network round-trips to achieve majority consensus via gRPC before responding.*

## Building the Project

### Prerequisites
* CMake 3.20+
* C++17 Compiler
* gRPC and Protobuf (`brew install grpc protobuf` on macOS)

### Build Steps
```bash
mkdir build && cd build
cmake ..
make
```

## Running the Cluster

1. **Start the nodes** (in separate terminal windows):
```bash
./raft_server 0
./raft_server 1
./raft_server 2
```

2. **Start the client interface:**
```bash
./raft_client
```

3. **Execute Commands:**
```text
raft-kv> PUT user:100 "Alice"
OK
raft-kv> GET user:100
Alice
raft-kv> DEL user:100
OK
```

## Testing & Fault Injection

The project includes an automated test suite (`tests/test_cluster.sh`) that injects failures to verify Raft's safety guarantees:
* **Leader Crash Recovery:** Kills the leader mid-operation and verifies that a new leader is elected and data remains consistent.
* **Follower Rejoin:** Kills a follower, continues writing to the cluster, then restarts the follower to verify it catches up via log replication or `InstallSnapshot` RPCs.
* **Majority Loss:** Simulates a network partition where a majority of nodes are unreachable, verifying that the system safely rejects writes instead of corrupting data.
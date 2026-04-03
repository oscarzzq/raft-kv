#!/bin/bash

SERVER="./cmake-build-debug/raft_server"
CLIENT="./cmake-build-debug/raft_client"

start_node() {
    rm -f /tmp/raft_node_$1.log
    $SERVER $1 > /tmp/raft_node_$1.log 2>&1 &
    local pid=$!
    echo $pid > /tmp/raft_pid_$1
    disown $pid
}

stop_all() {
    for i in 0 1 2; do
        if [ -f /tmp/raft_pid_$i ]; then
            kill $(cat /tmp/raft_pid_$i) 2>/dev/null
            rm -f /tmp/raft_pid_$i
        fi
    done
}

echo "Starting cluster for benchmarking..."
stop_all
rm -f /tmp/raft_snapshot_*.dat
start_node 0
start_node 1
start_node 2

# Wait for a leader to be elected
sleep 3

NUM_REQUESTS=2000
PAYLOAD_FILE="/tmp/raft_benchmark_payload.txt"

echo "Generating $NUM_REQUESTS requests..."
rm -f $PAYLOAD_FILE
for i in $(seq 1 $NUM_REQUESTS); do
    echo "PUT bench_key_$i bench_val_$i" >> $PAYLOAD_FILE
done
echo "exit" >> $PAYLOAD_FILE

echo "Running benchmark..."

# Capture start time in milliseconds
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    START_TIME=$(ruby -e 'puts (Time.now.to_f * 1000).to_i')
else
    # Linux
    START_TIME=$(date +%s%3N)
fi

# Pipe the payload into the client
cat $PAYLOAD_FILE | $CLIENT > /dev/null 2>&1

# Capture end time
if [[ "$OSTYPE" == "darwin"* ]]; then
    END_TIME=$(ruby -e 'puts (Time.now.to_f * 1000).to_i')
else
    END_TIME=$(date +%s%3N)
fi

DURATION_MS=$((END_TIME - START_TIME))
DURATION_SEC=$(echo "scale=3; $DURATION_MS / 1000" | bc)

OPS_PER_SEC=$(echo "scale=0; $NUM_REQUESTS * 1000 / $DURATION_MS" | bc)
LATENCY_MS=$(echo "scale=2; $DURATION_MS / $NUM_REQUESTS" | bc)

echo ""
echo "================================================"
echo " Benchmark Results"
echo "================================================"
echo " Total Requests: $NUM_REQUESTS"
echo " Total Time:     ${DURATION_SEC}s"
echo " Throughput:     $OPS_PER_SEC ops/sec"
echo " Avg Latency:    ${LATENCY_MS} ms/op"
echo "================================================"

stop_all
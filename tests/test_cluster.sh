#!/bin/bash

SERVER="./cmake-build-debug/raft_server"
CLIENT="./cmake-build-debug/raft_client"
PASS=0
FAIL=0

start_node() {
    rm -f /tmp/raft_node_$1.log
    $SERVER $1 > /tmp/raft_node_$1.log 2>&1 &
    local pid=$!
    echo $pid > /tmp/raft_pid_$1
    # Detach from job control to silence "Terminated: 15" logs
    disown $pid
}

stop_node() {
    if [ -f /tmp/raft_pid_$1 ]; then
        kill $(cat /tmp/raft_pid_$1) 2>/dev/null
        rm /tmp/raft_pid_$1
    fi
}

stop_all() {
    stop_node 0
    stop_node 1
    stop_node 2
    sleep 0.5
}

kv_put() {
    # Use sed to strip the prompt instead of grep -v dropping the line
    echo "PUT $1 $2" | $CLIENT 2>/dev/null | sed 's/raft-kv> *//g; s/\r//g' | grep -v "^$" | head -1
}

kv_get() {
    echo "GET $1" | $CLIENT 2>/dev/null | sed 's/raft-kv> *//g; s/\r//g' | grep -v "^$" | head -1
}

kv_del() {
    echo "DEL $1" | $CLIENT 2>/dev/null | sed 's/raft-kv> *//g; s/\r//g' | grep -v "^$" | head -1
}

assert_eq() {
    local test_name=$1
    local expected=$2
    local actual=$3
    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $test_name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $test_name"
        echo "    expected: '$expected'"
        echo "    actual:   '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

wait_for_leader() {
    # Dynamically poll for the leader rather than relying on a hardcoded sleep
    for _ in {1..10}; do
        if grep -q "becoming LEADER" /tmp/raft_node_*.log 2>/dev/null; then
            sleep 0.5 # Give the new leader a fraction of a second to initialize
            return
        fi
        sleep 0.5
    done
}

echo "================================================"
echo " raft-kv fault injection test suite"
echo "================================================"

# Test 1: Basic operations
echo ""
echo "Test 1: Basic PUT/GET/DEL"
stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

kv_put x 42 > /dev/null
kv_put y 99 > /dev/null

assert_eq "GET existing key"     "42"             "$(kv_get x)"
assert_eq "GET second key"       "99"             "$(kv_get y)"
assert_eq "DEL key"              "OK"             "$(kv_del x)"
assert_eq "GET deleted key"      "Error: key not found" "$(kv_get x)"

# Test 2: Leader crash
echo ""
echo "Test 2: Leader crash recovery"
stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

kv_put k1 hello > /dev/null

# Find which node is leader by checking logs
LEADER=-1
for i in 0 1 2; do
    if grep -q "becoming LEADER" /tmp/raft_node_$i.log 2>/dev/null; then
        LEADER=$i
        break
    fi
done

if [ $LEADER -ge 0 ]; then
    stop_node $LEADER
    wait_for_leader # Wait for the remaining nodes to elect a new leader

    assert_eq "Read after leader crash" "hello" "$(kv_get k1)"
    kv_put k2 world > /dev/null
    assert_eq "Write after leader crash" "world" "$(kv_get k2)"
else
    echo "  SKIP: could not detect leader"
fi

# Test 3: Follower crash
echo ""
echo "Test 3: Follower crash and rejoin"
stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

# Find a follower
FOLLOWER=-1
for i in 0 1 2; do
    if ! grep -q "becoming LEADER" /tmp/raft_node_$i.log 2>/dev/null; then
        FOLLOWER=$i
        break
    fi
done

if [ $FOLLOWER -ge 0 ]; then
    stop_node $FOLLOWER

    kv_put f1 aaa > /dev/null
    kv_put f2 bbb > /dev/null
    kv_put f3 ccc > /dev/null

    start_node $FOLLOWER
    sleep 1.5

    assert_eq "Rejoined follower GET f1" "aaa" "$(kv_get f1)"
    assert_eq "Rejoined follower GET f2" "bbb" "$(kv_get f2)"
    assert_eq "Rejoined follower GET f3" "ccc" "$(kv_get f3)"
else
    echo "  SKIP: could not detect follower"
fi

# Test 4: Majority loss
echo ""
echo "Test 4: Cluster unavailable without majority"
stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

kv_put m1 safe > /dev/null

stop_node 1
stop_node 2
sleep 0.5

RESULT=$(kv_put m2 unsafe)
assert_eq "Write fails without majority" "Error: could not reach cluster" "$RESULT"

# Test 5: Restart full cluster
echo ""
echo "Test 5: Full cluster restart"
stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

kv_put r1 persistent > /dev/null

stop_all
start_node 0
start_node 1
start_node 2
wait_for_leader

assert_eq "Data after full restart" "persistent" "$(kv_get r1)"

# Summary
echo ""
echo "================================================"
echo " Results: $PASS passed, $FAIL failed"
echo "================================================"

stop_all
exit $FAIL
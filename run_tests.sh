#!/bin/bash
# Helper script to compile and run the full resilience test suite

mkdir -p build && cd build
cmake -G Ninja ..
ninja

echo "-----------------------------------------------------"
echo "   Running Unit Tests (IPC, Load Balancing, Stats)   "
echo "-----------------------------------------------------"
./tests/test_core

echo "-----------------------------------------------------"
echo "   Running Hardware Resilience & Crash Simulations   "
echo "-----------------------------------------------------"
./tests/simulation_crash
./tests/test_node_graph_crash
./tests/test_load_balancing

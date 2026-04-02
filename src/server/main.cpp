#include "RaftNode.h"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: raft_server <node_id>\n";
        return 1;
    }

    int id = std::stoi(argv[1]);

    std::vector<std::string> peers = {
        "localhost:50051",
        "localhost:50052",
        "localhost:50053"
    };

    if (id < 0 || id >= (int)peers.size()) {
        std::cerr << "node_id must be 0, 1, or 2\n";
        return 1;
    }

    auto node = std::make_shared<RaftNode>(id, peers);

    if (id == 0) {
        std::thread([node]() {
            std::this_thread::sleep_for(1s);

            std::vector<std::string> commands = {
                "PUT x 1",
                "PUT y 2",
                "PUT z 3",
                "DEL x",
                "PUT x 99"
            };

            for (const auto& cmd : commands) {
                auto [index, term, is_leader] = node->submitCommand(cmd);
                if (is_leader) {
                    std::cout << "[TEST] Submitted '" << cmd
                              << "' at index=" << index
                              << " term=" << term << "\n";
                } else {
                    std::cout << "[TEST] Node 0 is not leader, skipping: "
                              << cmd << "\n";
                }
                std::this_thread::sleep_for(200ms);
            }
        }).detach();
    }

    node->start();
    return 0;
}
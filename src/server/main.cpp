#include "RaftNode.h"
#include <iostream>
#include <vector>
#include <string>

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

    RaftNode node(id, peers);
    node.start();

    return 0;
}
#include "RaftNode.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: raft_server <node_id>\n";
        return 1;
    }

    int id = std::stoi(argv[1]);

    // Hardcoded addresses for now
    std::vector<std::string> peers = {
        "localhost:50051",
        "localhost:50052",
        "localhost:50053"
    };

    RaftNode node(id, peers);
    node.start();

    std::cout << "Node " << id << " running\n";
    return 0;
}
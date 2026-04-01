//
// Created by Oscar Zhang on 2026/4/1.
//

#pragma once
#include <string>
#include <vector>
#include <mutex>

enum class NodeState { FOLLOWER, CANDIDATE, LEADER };

class RaftNode {
public:
    RaftNode(int id, std::vector<std::string> peer_addresses);
    void start();

private:
    int id_;
    NodeState state_;
    int current_term_;
    int voted_for_;        // -1 means no vote this term
    std::mutex mu_;        // protects all state above
    std::vector<std::string> peer_addresses_;
};
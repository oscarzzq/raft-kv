//
// Created by Oscar Zhang on 2026/4/1.
//

#include "RaftNode.h"
#include <iostream>

RaftNode::RaftNode(int id, std::vector<std::string> peer_addresses)
    : id_(id)
    , state_(NodeState::FOLLOWER)
    , current_term_(0)
    , voted_for_(-1)
    , peer_addresses_(peer_addresses)
{}

void RaftNode::start() {
    std::cout << "Node " << id_ << " starting as FOLLOWER\n";
}
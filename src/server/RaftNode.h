#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"

enum class NodeState { FOLLOWER, CANDIDATE, LEADER };

class RaftNode final : public raft::RaftService::Service {
public:
    RaftNode(int id, std::vector<std::string> peer_addresses);
    ~RaftNode();
    void start();

    grpc::Status RequestVote(
        grpc::ServerContext* context,
        const raft::RequestVoteRequest* request,
        raft::RequestVoteResponse* response
    ) override;

    grpc::Status AppendEntries(
        grpc::ServerContext* context,
        const raft::AppendEntriesRequest* request,
        raft::AppendEntriesResponse* response
    ) override;

private:
    int id_;
    NodeState state_;
    int current_term_;
    int voted_for_;
    int leader_id_;
    bool heartbeat_received_ = false;

    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> running_;

    std::vector<std::string> peer_addresses_;
    std::vector<std::unique_ptr<raft::RaftService::Stub>> peer_stubs_;

    std::thread election_thread_;
    std::thread heartbeat_thread_;
    std::unique_ptr<grpc::Server> grpc_server_;

    void runElectionTimer();
    void startElection(std::unique_lock<std::mutex>& lock);
    void runHeartbeat();
    void resetElectionTimer();

    int  getRandomTimeout();
    bool isMoreUpToDate(int last_log_index, int last_log_term);
    void becomeFollower(int term);
    void becomeLeader();
};
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
#include "KVStore.h"

enum class NodeState { FOLLOWER, CANDIDATE, LEADER };

struct LogEntry {
    int term;
    std::string command;
};

class RaftNode final
    : public raft::RaftService::Service
    , public raft::KVService::Service {
public:
    RaftNode(int id, std::vector<std::string> peer_addresses);
    ~RaftNode();
    void start();

    std::tuple<int, int, bool> submitCommand(const std::string& command);

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

    grpc::Status Execute(
        grpc::ServerContext* context,
        const raft::KVRequest* request,
        raft::KVResponse* response
    ) override;

private:
    int id_;
    NodeState state_;
    int current_term_;
    int voted_for_;
    int leader_id_;
    bool heartbeat_received_ = false;

    std::vector<LogEntry> log_;
    int commit_index_ = 0;
    int last_applied_ = 0;

    std::vector<int> next_index_;
    std::vector<int> match_index_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable commit_cv_;
    std::atomic<bool> running_;

    std::vector<std::string> peer_addresses_;
    std::vector<std::unique_ptr<raft::RaftService::Stub>> peer_stubs_;

    std::thread election_thread_;
    std::thread heartbeat_thread_;
    std::unique_ptr<grpc::Server> grpc_server_;

    KVStore kv_;

    void runElectionTimer();
    void startElection(std::unique_lock<std::mutex>& lock);
    void runHeartbeat();
    void sendAppendEntries(int peer_id);
    void resetElectionTimer();
    void advanceCommitIndex();
    void applyEntries();
    void applyToStateMachine(const std::string& command);
    void printLog();

    int  getRandomTimeout();
    bool isMoreUpToDate(int last_log_index, int last_log_term);
    void becomeFollower(int term);
    void becomeLeader();

    int lastLogIndex() { return (int)log_.size() - 1; }
    int lastLogTerm()  { return log_.empty() ? 0 : log_.back().term; }
};
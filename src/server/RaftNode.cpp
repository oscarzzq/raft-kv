#include "RaftNode.h"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>
#include <grpcpp/grpcpp.h>
#include <sstream>

using namespace std::chrono_literals;

RaftNode::RaftNode(int id, std::vector<std::string> peer_addresses)
    : id_(id)
    , state_(NodeState::FOLLOWER)
    , current_term_(0)
    , voted_for_(-1)
    , leader_id_(-1)
    , running_(true)
    , peer_addresses_(peer_addresses)
{
    log_.push_back({0, ""});

    for (const auto& addr : peer_addresses_) {
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        peer_stubs_.push_back(raft::RaftService::NewStub(channel));
    }
}

RaftNode::~RaftNode() {
    running_ = false;
    cv_.notify_all();
    commit_cv_.notify_all();
    if (election_thread_.joinable())  election_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (grpc_server_) grpc_server_->Shutdown();
}

void RaftNode::start() {
    std::string address = peer_addresses_[id_];
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(static_cast<raft::RaftService::Service*>(this));
    builder.RegisterService(static_cast<raft::KVService::Service*>(this));
    grpc_server_ = builder.BuildAndStart();
    std::cout << "Node " << id_ << " listening on " << address << std::endl;

    election_thread_  = std::thread(&RaftNode::runElectionTimer, this);
    heartbeat_thread_ = std::thread(&RaftNode::runHeartbeat, this);

    grpc_server_->Wait();
}

std::tuple<int, int, bool> RaftNode::submitCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(mu_);

    if (state_ != NodeState::LEADER) {
        return {-1, -1, false};
    }

    LogEntry entry{current_term_, command};
    log_.push_back(entry);
    int index = lastLogIndex();

    std::cout << "Node " << id_ << " appended entry at index "
              << index << ": " << command << std::endl;

    return {index, current_term_, true};
}

int RaftNode::getRandomTimeout() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(150, 300);
    return dist(rng);
}

void RaftNode::resetElectionTimer() {
    heartbeat_received_ = true;
    cv_.notify_all();
}

void RaftNode::becomeFollower(int term) {
    std::cout << "Node " << id_ << " becoming FOLLOWER (term " << term << ")" << std::endl;
    state_        = NodeState::FOLLOWER;
    current_term_ = term;
    voted_for_    = -1;
    leader_id_    = -1;
}

void RaftNode::becomeLeader() {
    std::cout << "Node " << id_ << " becoming LEADER (term "
              << current_term_ << ")" << std::endl;
    state_     = NodeState::LEADER;
    leader_id_ = id_;

    int n = (int)peer_addresses_.size();
    next_index_.assign(n, lastLogIndex() + 1);
    match_index_.assign(n, 0);
    match_index_[id_] = lastLogIndex();

    log_.push_back({current_term_, ""});
    match_index_[id_] = lastLogIndex();

    cv_.notify_all();
}

bool RaftNode::isMoreUpToDate(int last_log_index, int last_log_term) {
    int my_last_term  = lastLogTerm();
    int my_last_index = lastLogIndex();

    if (last_log_term != my_last_term) {
        return last_log_term >= my_last_term;
    }
    return last_log_index >= my_last_index;
}

void RaftNode::advanceCommitIndex() {
    int n = (int)peer_addresses_.size();
    int majority = n / 2 + 1;

    for (int idx = lastLogIndex(); idx > commit_index_; idx--) {
        if (log_[idx].term != current_term_) continue;

        int count = 0;
        for (int i = 0; i < n; i++) {
            if (match_index_[i] >= idx) count++;
        }

        if (count >= majority) {
            commit_index_ = idx;
            std::cout << "Node " << id_ << " committed up to index "
                      << commit_index_ << std::endl;
            commit_cv_.notify_all();
            break;
        }
    }
}

void RaftNode::applyEntries() {
    while (last_applied_ < commit_index_) {
        last_applied_++;
        auto& entry = log_[last_applied_];
        applyToStateMachine(entry.command);
    }
}

void RaftNode::applyToStateMachine(const std::string& command) {
    if (command.empty()) return;

    std::istringstream ss(command);
    std::string op, key, value;
    ss >> op >> key;

    if (op == "PUT") {
        ss >> value;
        kv_.put(key, value);
        std::cout << "Node " << id_ << " applied PUT " << key << "=" << value << std::endl;
    } else if (op == "DEL") {
        kv_.del(key);
        std::cout << "Node " << id_ << " applied DEL " << key << std::endl;
    }
}

void RaftNode::runElectionTimer() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mu_);

        heartbeat_received_ = false;
        int timeout_ms = getRandomTimeout();

        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return !running_ || state_ == NodeState::LEADER || heartbeat_received_;
        });

        if (!running_) break;
        if (state_ == NodeState::LEADER || heartbeat_received_) continue;

        startElection(lock);
    }
}

void RaftNode::startElection(std::unique_lock<std::mutex>& lock) {
    state_        = NodeState::CANDIDATE;
    current_term_++;
    voted_for_    = id_;
    int term      = current_term_;
    int votes     = 1;
    int majority  = (peer_addresses_.size() / 2) + 1;

    std::cout << "Node " << id_ << " starting election for term " << term << std::endl;

    raft::RequestVoteRequest req;
    req.set_term(term);
    req.set_candidate_id(id_);
    req.set_last_log_index(lastLogIndex());
    req.set_last_log_term(lastLogTerm());

    lock.unlock();

    std::mutex votes_mu;
    std::vector<std::thread> vote_threads;

    for (int i = 0; i < (int)peer_stubs_.size(); i++) {
        if (i == id_) continue;

        vote_threads.emplace_back([&, i]() {
            raft::RequestVoteResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + 50ms);

            auto status = peer_stubs_[i]->RequestVote(&ctx, req, &resp);
            if (!status.ok()) return;

            std::lock_guard<std::mutex> lk(mu_);

            if (resp.term() > current_term_) {
                becomeFollower(resp.term());
                return;
            }

            if (state_ != NodeState::CANDIDATE || current_term_ != term) return;

            if (resp.vote_granted()) {
                std::lock_guard<std::mutex> vlk(votes_mu);
                votes++;
                if (votes >= majority) becomeLeader();
            }
        });
    }

    for (auto& t : vote_threads) t.join();
    lock.lock();
}

void RaftNode::runHeartbeat() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mu_);

        cv_.wait(lock, [this] {
            return !running_ || state_ == NodeState::LEADER;
        });

        if (!running_) break;

        for (int i = 0; i < (int)peer_stubs_.size(); i++) {
            if (i == id_) continue;
            std::thread(&RaftNode::sendAppendEntries, this, i).detach();
        }

        lock.unlock();
        std::this_thread::sleep_for(50ms);
        lock.lock();
    }
}

void RaftNode::sendAppendEntries(int peer_id) {
    std::unique_lock<std::mutex> lock(mu_);

    if (state_ != NodeState::LEADER) return;

    int next  = next_index_[peer_id];
    int prev_log_index = next - 1;
    int prev_log_term  = (prev_log_index > 0) ? log_[prev_log_index].term : 0;

    raft::AppendEntriesRequest req;
    req.set_term(current_term_);
    req.set_leader_id(id_);
    req.set_prev_log_index(prev_log_index);
    req.set_prev_log_term(prev_log_term);
    req.set_leader_commit(commit_index_);

    for (int i = next; i <= lastLogIndex(); i++) {
        auto* e = req.add_entries();
        e->set_term(log_[i].term);
        e->set_command(log_[i].command);
    }

    lock.unlock();

    raft::AppendEntriesResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + 30ms);
    auto status = peer_stubs_[peer_id]->AppendEntries(&ctx, req, &resp);

    if (!status.ok()) return;

    lock.lock();

    if (resp.term() > current_term_) {
        becomeFollower(resp.term());
        return;
    }

    if (state_ != NodeState::LEADER) return;

    if (resp.success()) {
        int new_match = prev_log_index + (int)req.entries_size();
        match_index_[peer_id] = std::max(match_index_[peer_id], new_match);
        next_index_[peer_id]  = match_index_[peer_id] + 1;

        match_index_[id_] = lastLogIndex();
        advanceCommitIndex();
        applyEntries();
    } else {
        if (next_index_[peer_id] > 1) {
            next_index_[peer_id]--;
        }
    }
}

grpc::Status RaftNode::RequestVote(
    grpc::ServerContext* context,
    const raft::RequestVoteRequest* request,
    raft::RequestVoteResponse* response)
{
    std::lock_guard<std::mutex> lock(mu_);

    if (request->term() > current_term_) {
        becomeFollower(request->term());
    }

    response->set_term(current_term_);

    if (request->term() < current_term_) {
        response->set_vote_granted(false);
        return grpc::Status::OK;
    }

    bool can_vote = (voted_for_ == -1 || voted_for_ == request->candidate_id());
    bool log_ok   = isMoreUpToDate(request->last_log_index(), request->last_log_term());

    if (can_vote && log_ok) {
        voted_for_ = request->candidate_id();
        response->set_vote_granted(true);
        resetElectionTimer();
        std::cout << "Node " << id_ << " voting for "
                  << request->candidate_id() << " in term " << current_term_ << std::endl;
    } else {
        response->set_vote_granted(false);
    }

    return grpc::Status::OK;
}

grpc::Status RaftNode::AppendEntries(
    grpc::ServerContext* context,
    const raft::AppendEntriesRequest* request,
    raft::AppendEntriesResponse* response)
{
    std::lock_guard<std::mutex> lock(mu_);

    if (request->term() > current_term_) {
        becomeFollower(request->term());
    }

    response->set_term(current_term_);

    if (request->term() < current_term_) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    leader_id_ = request->leader_id();
    resetElectionTimer();

    int prev_index = request->prev_log_index();
    int prev_term  = request->prev_log_term();

    if (prev_index > lastLogIndex() ||
        (prev_index > 0 && log_[prev_index].term != prev_term)) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    int idx = prev_index + 1;
    for (const auto& entry : request->entries()) {
        if (idx <= lastLogIndex()) {
            if (log_[idx].term != entry.term()) {
                log_.erase(log_.begin() + idx, log_.end());
            } else {
                idx++;
                continue;
            }
        }
        log_.push_back({entry.term(), entry.command()});
        idx++;
    }

    if (request->leader_commit() > commit_index_) {
        commit_index_ = std::min(request->leader_commit(), lastLogIndex());
        applyEntries();
    }

    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status RaftNode::Execute(
    grpc::ServerContext* context,
    const raft::KVRequest* request,
    raft::KVResponse* response)
{
    const std::string& op  = request->op();
    const std::string& key = request->key();

    if (op == "GET") {
        std::lock_guard<std::mutex> lock(mu_);
        auto val = kv_.get(key);
        if (val) {
            response->set_success(true);
            response->set_value(*val);
        } else {
            response->set_success(false);
            response->set_error("key not found");
        }
        return grpc::Status::OK;
    }

    std::string command = op + " " + key;
    if (op == "PUT") command += " " + request->value();

    std::unique_lock<std::mutex> lock(mu_);

    if (state_ != NodeState::LEADER) {
        response->set_success(false);
        response->set_not_leader(true);
        response->set_error("not leader");
        response->set_leader_id(leader_id_);
        return grpc::Status::OK;
    }

    LogEntry entry{current_term_, command};
    log_.push_back(entry);
    int index = lastLogIndex();
    match_index_[id_] = index;

    commit_cv_.wait_for(lock, std::chrono::milliseconds(2000), [this, index] {
        return commit_index_ >= index || state_ != NodeState::LEADER;
    });

    if (commit_index_ >= index) {
        response->set_success(true);
    } else {
        response->set_success(false);
        response->set_error("commit timeout or leader changed");
    }

    return grpc::Status::OK;
}
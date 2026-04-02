//
// Created by Oscar Zhang on 2026/4/1.
//

#include <iostream>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"

class KVClient {
public:
    KVClient(std::vector<std::string> addresses) : addresses_(addresses), leader_(0) {
        connect(leader_);
    }

    void run() {
        std::string line;
        std::cout << "raft-kv> ";
        while (std::getline(std::cin, line)) {
            if (line.empty()) { std::cout << "raft-kv> "; continue; }
            if (line == "exit") break;
            process(line);
            std::cout << "raft-kv> ";
        }
    }

private:
    std::vector<std::string> addresses_;
    int leader_;
    std::unique_ptr<raft::KVService::Stub> stub_;

    void connect(int node) {
        leader_ = node;
        auto channel = grpc::CreateChannel(addresses_[node], grpc::InsecureChannelCredentials());
        stub_ = raft::KVService::NewStub(channel);
    }

    void process(const std::string& line) {
        std::istringstream ss(line);
        std::string op, key, value;
        ss >> op >> key;

        if (op != "GET" && op != "PUT" && op != "DEL") {
            std::cout << "Unknown command. Usage: GET <key> | PUT <key> <value> | DEL <key>\n";
            return;
        }

        raft::KVRequest req;
        req.set_op(op);
        req.set_key(key);

        if (op == "PUT") {
            ss >> value;
            req.set_value(value);
        }

        send(req);
    }

    void send(const raft::KVRequest& req, int attempts = 0) {
        if (attempts >= 3) {
            std::cout << "Error: could not reach cluster\n";
            return;
        }

        raft::KVResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));

        auto status = stub_->Execute(&ctx, req, &resp);

        if (!status.ok()) {
            int next = (leader_ + 1) % addresses_.size();
            connect(next);
            send(req, attempts + 1);
            return;
        }

        if (!resp.success()) {
            if (resp.not_leader() && resp.leader_id() >= 0 && resp.leader_id() < (int)addresses_.size()) {
                connect(resp.leader_id());
                send(req, attempts + 1);
            } else {
                std::cout << "Error: " << resp.error() << "\n";
            }
            return;
        }

        if (!resp.value().empty()) {
            std::cout << resp.value() << "\n";
        } else {
            std::cout << "OK\n";
        }
    }
};

int main() {
    std::vector<std::string> peers = {
        "localhost:50051",
        "localhost:50052",
        "localhost:50053"
    };

    KVClient client(peers);
    client.run();
    return 0;
}
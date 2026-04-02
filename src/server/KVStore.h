//
// Created by Oscar Zhang on 2026/4/2.
//
#pragma once
#include <string>
#include <unordered_map>
#include <optional>

class KVStore {
public:
    void put(const std::string& key, const std::string& value) {
        store_[key] = value;
    }

    std::optional<std::string> get(const std::string& key) const {
        auto it = store_.find(key);
        if (it == store_.end()) return std::nullopt;
        return it->second;
    }

    bool del(const std::string& key) {
        return store_.erase(key) > 0;
    }

private:
    std::unordered_map<std::string, std::string> store_;
};
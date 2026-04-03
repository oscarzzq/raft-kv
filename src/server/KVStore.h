#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <sstream>

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

    std::string serialize() const {
        std::string out;
        for (const auto& [k, v] : store_) {
            out += k + "\t" + v + "\n";
        }
        return out;
    }

    void deserialize(const std::string& data) {
        store_.clear();
        std::istringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            auto sep = line.find('\t');
            if (sep != std::string::npos) {
                store_[line.substr(0, sep)] = line.substr(sep + 1);
            }
        }
    }

private:
    std::unordered_map<std::string, std::string> store_;
};
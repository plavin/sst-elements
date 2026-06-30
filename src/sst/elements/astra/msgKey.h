#pragma once

#include <cstddef>
#include <functional>
#include <string>

class MsgKey {
public:
    int src;
    int dst;
    int tag;

    MsgKey() = default;

    MsgKey(int src_, int dst_, int tag_)
        : src(src_), dst(dst_), tag(tag_) {}

    std::string to_string() const {
        return "(" + std::to_string(src) + ", " +
                     std::to_string(dst) + ", " +
                     std::to_string(tag) + ")";
    }

    bool operator==(const MsgKey& other) const noexcept {
        return src == other.src && dst == other.dst && tag == other.tag;
    }
};

struct MsgKeyHash {
    std::size_t operator()(const MsgKey& t) const noexcept {
        std::size_t h1 = std::hash<int>{}(t.src);
        std::size_t h2 = std::hash<int>{}(t.dst);
        std::size_t h3 = std::hash<int>{}(t.tag);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

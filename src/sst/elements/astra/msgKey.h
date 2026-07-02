#pragma once

#include <cstddef>
#include <functional>
#include <string>

using nid_t = int64_t;

// Messages in AstraSim are uniquely defined by <src, dst, tag>
class MsgKey {
public:
    nid_t src;
    nid_t dst;
    int tag;

    MsgKey() = default;

    MsgKey(nid_t src_, nid_t dst_, nid_t tag_)
        : src(src_), dst(dst_), tag(tag_) {}

    bool operator==(const MsgKey& other) const noexcept {
        return src == other.src && dst == other.dst && tag == other.tag;
    }
};

struct MsgKeyHash {
    std::size_t operator()(const MsgKey& t) const noexcept {
        std::size_t h1 = std::hash<nid_t>{}(t.src);
        std::size_t h2 = std::hash<nid_t>{}(t.dst);
        std::size_t h3 = std::hash<int>{}(t.tag);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

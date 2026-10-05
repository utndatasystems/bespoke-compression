#pragma once
#include "onpair16.h"
#include <span>
#include <stdexcept>
struct RowArchive {
    OnPair16 decoder;
    uint64_t original_bytes = 0;
    void load(std::span<const std::byte> archive);
};

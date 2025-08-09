#pragma once
#include "Bench_StateHash.hpp"
#include <cstring>

// ----------------------------------------------------
// xxHash3 128-bit minimal implementation
// Based on Yann Collet's xxhash (public domain/BSD).
// Stripped to only the pieces we need for 128-bit state.
// ----------------------------------------------------

#define XXH_INLINE_ALL
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"  // Include xxhash.h from https://github.com/Cyan4973/xxHash

static XXH3_state_t g_state;

// Combine an entity id into the stream before the raw bytes.
// This prevents collisions if two entities happen to serialize to identical bytes.
static inline void hash_entity_block(uint64_t entity_id, const void* bytes, size_t len) {
    XXH3_128bits_update(&g_state, &entity_id, sizeof(entity_id));
    XXH3_128bits_update(&g_state, bytes, len);
}

extern "C" {
void HashBegin(uint64_t frame_idx, uint64_t build_id, uint64_t seed) {
    XXH3_128bits_reset_withSeed(&g_state, seed ^ (frame_idx * 0x9E3779B185EBCA87ULL) ^ build_id);
}

void HashAccumulateEntity(uint64_t entity_id, const void* bytes, size_t len) {
    hash_entity_block(entity_id, bytes, len);
}

void HashEnd(uint64_t out_hash[2]) {
    XXH128_hash_t h = XXH3_128bits_digest(&g_state);
    out_hash[0] = h.high64;
    out_hash[1] = h.low64;
}
} // extern "C"
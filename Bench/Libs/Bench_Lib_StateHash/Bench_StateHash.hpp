#pragma once
#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

	void HashBegin(uint64_t frame_idx, uint64_t build_id, uint64_t seed);
	void HashAccumulateEntity(uint64_t entity_id, const void* bytes, size_t len);
	void HashEnd(uint64_t out_hash[2]);

#ifdef __cplusplus
}
#endif

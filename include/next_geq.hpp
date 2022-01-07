#pragma once

namespace sliced {

#define BLOCK_MIN                                                            \
    if (LIKELY(*(begin + 1) < 30)) return *data + base;                      \
    return min_value_in_bitmap(data, constants::block_size_in_64bit_words) + \
           base;

#define BLOCK_MIN_                                                          \
    if (LIKELY(*(m_begin + 1) < 30)) {                                      \
        value = *m_data + base;                                             \
    } else {                                                                \
        value = min_value_in_bitmap(m_data,                                 \
                                    constants::block_size_in_64bit_words) + \
                base;                                                       \
    }

#define CHUNK_MIN(it)                                                          \
    switch (it.type()) {                                                       \
        case type::sparse:                                                     \
            value = min_value_in_sparse_chunk(it.data, it.blocks());           \
            break;                                                             \
        case type::dense:                                                      \
            value = min_value_in_bitmap(it.data,                               \
                                        constants::chunk_size_in_64bit_words); \
            break;                                                             \
        case type::full:                                                       \
            value = 0;                                                         \
            break;                                                             \
        default:                                                               \
            assert(false);                                                     \
            __builtin_unreachable();                                           \
    }                                                                          \
    return value + it.base();

uint32_t next_geq_sparse_block(uint8_t const* begin, int cardinality,
                               uint32_t value) {
    for (int i = 0; i != cardinality; ++i) {
        if (begin[i] >= value) return begin[i];
    }
    return constants::not_found;
}

uint32_t max_value_in_bitmap(uint8_t const* data, size_t size_in_64bit_words) {
    uint64_t const* bitmap = reinterpret_cast<uint64_t const*>(data);
    for (int32_t i = size_in_64bit_words - 1; i >= 0; --i) {
        uint64_t w = bitmap[i];
        if (w != 0) {
            int r = __builtin_clzll(w);
            return i * 64 + 63 - r;
        }
    }
    return 0;
}

uint32_t min_value_in_bitmap(uint8_t const* data, size_t size_in_64bit_words) {
    uint64_t const* bitmap = reinterpret_cast<uint64_t const*>(data);
    for (uint32_t i = 0; i != size_in_64bit_words; ++i) {
        uint64_t w = bitmap[i];
        if (w != 0) return i * 64 + __builtin_ctzll(w);
    }
    return 0;
}

uint32_t next_geq_bitmap(uint8_t const* data, uint32_t size_in_64bit_words,
                         uint32_t value) {
    uint64_t const* bitmap = reinterpret_cast<uint64_t const*>(data);
    uint32_t k = value / 64;
    uint64_t word = bitmap[k];
    const int diff = value - k * 64;
    word = (word >> diff) << diff;
    while (word == 0) {
        k++;
        if (k == size_in_64bit_words) return constants::not_found;
        word = bitmap[k];
    }
    return k * 64 + __builtin_ctzll(word);
}

uint32_t max_value_in_block(uint8_t const* begin, uint8_t const* data) {
    int c = *(begin + 1);
    if (LIKELY(c < 30)) {  // block type is sparse
        return *(data + c);
    }
    return max_value_in_bitmap(data, constants::block_size_in_64bit_words);
}

uint32_t next_geq_sparse_chunk(uint8_t const* begin, int blocks,
                               uint32_t value) {
    assert(blocks >= 1 and blocks <= 256);
    uint8_t const* data = begin + blocks * 2;
    uint8_t const* end = data;
    uint32_t block_id = value >> 8;
    uint32_t id = *begin;

    while (id < block_id and begin != end) {
        int c = *(begin + 1) + 1;
        data += BYTES_BY_CARDINALITY(c);
        begin += 2;
        id = *begin;
    }

    if (begin != end) {
        uint32_t base = id * 256;
        if (base >= value) {  // saturate
            BLOCK_MIN
        }

        value &= 0xFF;
        if (value > max_value_in_block(begin, data)) {  // saturate
            if (begin + 2 == end) return constants::not_found;
            int c = *(begin + 1) + 1;
            data += BYTES_BY_CARDINALITY(c);
            begin += 2;
            id = *begin;
            base = id * 256;
            BLOCK_MIN
        }

        base = id * 256;
        int c = *(begin + 1) + 1;
        if (LIKELY(c < 31)) {  // block type is sparse
            return next_geq_sparse_block(data, c, value) + base;
        }
        return next_geq_bitmap(data, constants::block_size_in_64bit_words,
                               value) +
               base;
    }

    return constants::not_found;
}

uint32_t min_value_in_sparse_chunk(uint8_t const* begin, int blocks) {
    assert(blocks >= 1 and blocks <= 256);
    uint8_t const* data = begin + blocks * 2;
    uint32_t id = *begin;
    uint32_t base = id * 256;
    BLOCK_MIN
}

uint32_t s_sequence::next_geq(uint32_t value) const {
    auto it = begin();
    uint32_t chunk_id = value >> 16;
    it.skip_to_value(chunk_id);

    if (it.base() >= value) {  // saturate
        CHUNK_MIN(it)
    }

    if (it.has_next()) {
        value &= 0xFFFF;
        switch (it.type()) {
            case type::sparse:
                value = next_geq_sparse_chunk(it.data, it.blocks(), value);
                break;
            case type::dense:
                value = next_geq_bitmap(
                    it.data, constants::chunk_size_in_64bit_words, value);
                break;
            case type::full:
                break;
            default:
                assert(false);
                __builtin_unreachable();
        }

        if (value != constants::not_found) return value + it.base();

        // saturate
        it.next();
        if (it.has_next()) CHUNK_MIN(it)
    }

    return constants::not_found;
}

}  // namespace sliced
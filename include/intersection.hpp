#pragma once

#include "immintrin.h"

#include "constants.hpp"
#include "util.hpp"
#include "decode.hpp"
#include "uncompress.hpp"
#include "table.hpp"

namespace sliced {

#define INIT                                          \
    __m256i base_v = _mm256_set1_epi32(base);         \
    __m128i v_l = _mm_lddqu_si128((__m128i const*)l); \
    __m128i v_r = _mm_lddqu_si128((__m128i const*)r); \
    __m256i converted_v;                              \
    __m128i shuf, p, res;                             \
    int mask, matched;

#define INTERSECT                                                             \
    res =                                                                     \
        _mm_cmpestrm(v_l, card_l, v_r, card_r,                                \
                     _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK); \
    mask = _mm_extract_epi32(res, 0);                                         \
    matched = _mm_popcnt_u32(mask);                                           \
    size += matched;                                                          \
    shuf = _mm_load_si128((__m128i const*)shuffle_mask + mask);               \
    p = _mm_shuffle_epi8(v_r, shuf);                                          \
    converted_v = _mm256_cvtepu8_epi32(p);                                    \
    converted_v = _mm256_add_epi32(base_v, converted_v);                      \
    _mm256_storeu_si256((__m256i*)out, converted_v);                          \
    if (matched > 8) {                                                        \
        p = _mm_bsrli_si128(p, 8);                                            \
        converted_v = _mm256_cvtepu8_epi32(p);                                \
        converted_v = _mm256_add_epi32(base_v, converted_v);                  \
        _mm256_storeu_si256((__m256i*)(out + 8), converted_v);                \
    }

#define ADVANCE(ptr)                                \
    out += size;                                    \
    ptr += 16;                                      \
    v_##ptr = _mm_lddqu_si128((__m128i const*)ptr); \
    card_##ptr -= 16;

size_t ss_intersect_block(uint8_t const* l, uint8_t const* r, int card_l,
                          int card_r, uint32_t base, uint32_t* out) {
    assert(card_l > 0 and
           card_l <= int(constants::block_sparseness_threshold - 2));
    assert(card_r > 0 and
           card_r <= int(constants::block_sparseness_threshold - 2));
    size_t size = 0;

    if (LIKELY(card_l <= 16 and card_r <= 16)) {
        INIT INTERSECT return size;  // 1 cmpestr
    }

    if (card_l <= 16 and card_r > 16) {
        INIT INTERSECT ADVANCE(r) INTERSECT return size;  // 2 cmpestr
    }

    if (card_r <= 16 and card_l > 16) {
        INIT INTERSECT ADVANCE(l) INTERSECT return size;  // 2 cmpestr
    }

    // card_l  > 16 and card_r  > 16 -> 4 cmpestr, but scalar may be more
    // convenient...

    uint8_t const* end_l = l + card_l;
    uint8_t const* end_r = r + card_r;
    while (true) {
        while (*l < *r) {
            if (++l == end_l) return size;
        }
        while (*l > *r) {
            if (++r == end_r) return size;
        }
        if (*l == *r) {
            out[size++] = *l + base;
            if (++l == end_l or ++r == end_r) return size;
        }
    }

    return size;
}

inline size_t dd_intersect_block(uint8_t const* l, uint8_t const* r,
                                 uint32_t base, uint32_t* out) {
    return and_bitmaps(l, r, constants::block_size_in_64bit_words, base, out);
}

size_t ds_intersect_block(uint8_t const* l, uint8_t const* r, int card,
                          uint32_t base, uint32_t* out) {
    uint64_t const* bitmap = reinterpret_cast<uint64_t const*>(l);
    uint32_t k = 0;
    for (int i = 0; i != card; ++i) {
        uint32_t key = r[i];
        out[k] = key + base;
        k += bitmap_contains(bitmap, key);
    }
    return k;
}

size_t ss_intersect_chunk(uint8_t const* l, uint8_t const* r, int blocks_l,
                          int blocks_r, uint32_t base, uint32_t* out) {
    assert(blocks_l >= 1 and blocks_l <= 256);
    assert(blocks_r >= 1 and blocks_r <= 256);
    uint8_t const* data_l = l + blocks_l * 2;
    uint8_t const* data_r = r + blocks_r * 2;
    uint8_t const* end_l = data_l;
    uint8_t const* end_r = data_r;
    uint32_t* tmp = out;

    while (true) {
        while (*l < *r) {
            if (l + 2 == end_l) return size_t(tmp - out);
            int c = *(l + 1) + 1;
            data_l += BYTES_BY_CARDINALITY(c);
            l += 2;
        }
        while (*l > *r) {
            if (r + 2 == end_r) return size_t(tmp - out);
            int c = *(r + 1) + 1;
            data_r += BYTES_BY_CARDINALITY(c);
            r += 2;
        }
        if (*l == *r) {
            uint8_t id = *l;
            ++l;
            ++r;
            int cl = *l + 1;
            int cr = *r + 1;
            int type_l = type::dense;
            int type_r = type::dense;
            int bl = 32;
            int br = 32;

            if (LIKELY(cl < 31)) {
                bl = cl;
                type_l = type::sparse;
            }

            if (LIKELY(cr < 31)) {
                br = cr;
                type_r = type::sparse;
            }

            uint32_t b = base + id * 256;
            uint32_t n = 0;

            switch (block_pair(type_l, type_r)) {
                case block_pair(type::sparse, type::sparse):
                    n = ss_intersect_block(data_l, data_r, cl, cr, b, tmp);
                    break;
                case block_pair(type::sparse, type::dense):
                    n = ds_intersect_block(data_r, data_l, cl, b, tmp);
                    break;
                case block_pair(type::dense, type::sparse):
                    n = ds_intersect_block(data_l, data_r, cr, b, tmp);
                    break;
                case block_pair(type::dense, type::dense):
                    n = and_bitmaps(data_l, data_r,
                                    constants::block_size_in_64bit_words, b,
                                    tmp);
                    break;
                default:
                    assert(false);
                    __builtin_unreachable();
            }

            tmp += n;

            if (l + 1 == end_l or r + 1 == end_r) return size_t(tmp - out);

            data_l += bl;
            data_r += br;
            ++l;
            ++r;
        }
    }

    return size_t(tmp - out);
}

size_t ds_intersect_chunk(uint8_t const* l, uint8_t const* r, int blocks_r,
                          uint32_t base, uint32_t* out) {
    static std::vector<uint64_t> x(constants::chunk_size_in_64bit_words);
    std::fill(x.begin(), x.end(), 0);
    uncompress_sparse_chunk(r, blocks_r, x.data());
    return and_bitmaps(l, reinterpret_cast<uint8_t const*>(x.data()),
                       constants::chunk_size_in_64bit_words, base, out);
}

size_t pairwise_intersection(s_sequence const& l, s_sequence const& r,
                             uint32_t* out) {
    auto it_l = l.begin();
    auto it_r = r.begin();
    uint32_t* in = out;
    while (it_l.has_next() and it_r.has_next()) {
        uint16_t id_l = it_l.id();
        uint16_t id_r = it_r.id();

        if (id_l == id_r) {
            uint32_t n = 0;
            uint32_t base = id_l << 16;
            int blocks_l = 0;
            int blocks_r = 0;

            uint16_t type_l = it_l.type();
            uint16_t type_r = it_r.type();

            switch (chunk_pair(type_l, type_r)) {
                case chunk_pair(type::sparse, type::sparse):
                    blocks_l = it_l.blocks();
                    blocks_r = it_r.blocks();
                    if (blocks_l < blocks_r) {
                        n = ss_intersect_chunk(it_l.data, it_r.data, blocks_l,
                                               blocks_r, base, out);
                    } else {
                        n = ss_intersect_chunk(it_r.data, it_l.data, blocks_r,
                                               blocks_l, base, out);
                    }
                    break;
                case chunk_pair(type::sparse, type::dense):
                    n = ds_intersect_chunk(it_r.data, it_l.data, it_l.blocks(),
                                           base, out);
                    break;
                case chunk_pair(type::sparse, type::full):
                    n = decode_sparse_chunk(it_l.data, it_l.blocks(), base,
                                            out);
                    break;
                case chunk_pair(type::dense, type::sparse):
                    n = ds_intersect_chunk(it_l.data, it_r.data, it_r.blocks(),
                                           base, out);
                    break;
                case chunk_pair(type::dense, type::dense):
                    n = and_bitmaps(it_l.data, it_r.data,
                                    constants::chunk_size_in_64bit_words, base,
                                    out);
                    break;
                case chunk_pair(type::dense, type::full):
                    n = decode_bitmap(
                        reinterpret_cast<uint64_t const*>(it_l.data),
                        constants::chunk_size_in_64bit_words, base, out);
                    break;
                case chunk_pair(type::full, type::sparse):
                    n = decode_sparse_chunk(it_r.data, it_r.blocks(), base,
                                            out);
                    break;
                case chunk_pair(type::full, type::dense):
                    n = decode_bitmap(
                        reinterpret_cast<uint64_t const*>(it_r.data),
                        constants::chunk_size_in_64bit_words, base, out);
                    break;
                case chunk_pair(type::full, type::full):
                    n = decode_full_chunk(base, out);
                    break;
                default:
                    assert(false);
                    __builtin_unreachable();
            }

            out += n;
            it_l.next();
            it_r.next();

        } else if (id_l < id_r) {
            it_l.advance(id_r);
        } else {
            it_r.advance(id_l);
        }
    }
    return size_t(out - in);
}

}  // namespace sliced
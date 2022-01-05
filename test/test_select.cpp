#include <iostream>

#include "../external/mm_file/include/mm_file/mm_file.hpp"

#include "test_common.hpp"
#include "util.hpp"
#include "s_index.hpp"
#include "select.hpp"

using namespace sliced;

void test(char const* binary_filename, parameters const& params) {
    s_index index;
    index.mmap(binary_filename);

    mm::file_source<uint32_t> input(params.collection_filename,
                                    mm::advice::sequential);
    uint32_t const* data = input.data();
    assert(data[0] == 1);
    std::cout << "universe size: " << index.universe() << std::endl;
    size_t k = 0;
    bool good = true;

    for (size_t i = 2; i < input.size();) {
        uint32_t n = data[i];
        uint32_t universe = data[i + n];
        if (pass(params, n, universe)) {
            auto sequence = index[k];
            uint32_t c = sequence.cardinality();

            if (c != n) {
                good = false;
                std::cout << "cardinality " << c << ": expected " << n
                          << std::endl;
            }

            uint32_t const* ptr = data + i + 1;
            uint32_t value = 0;
            for (size_t j = 0; j != n; ++j) {
                uint32_t expected = *ptr++;

                bool valid = sequence.select(j, value);
                if (!valid) {
                    good = false;
                    std::cout << "rank " << j << " should have been valid"
                              << std::endl;
                }

                if (expected != value) {
                    good = false;
                    std::cout << "error at " << j << "/" << n << ": expected "
                              << expected << " but got " << value << std::endl;
                }
            }

            ++k;
            if (k % 1000 == 0) {
                std::cout << "checked " << k << " sequences" << std::endl;
            }
        }
        i += n + 1;
    }
    std::cout << "checked " << k << " sequences" << std::endl;
    if (good) std::cout << "everything good" << std::endl;
}

int main(int argc, char** argv) {
    TEST return 0;
}

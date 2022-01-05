#include <iostream>

#include "../external/essentials/include/essentials.hpp"
#include "builder.hpp"
#include "s_sequence.hpp"
#include "select.hpp"
#include "decode.hpp"
#include "intersection.hpp"
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

using namespace sliced;
using namespace std;


std::vector<uint32_t>* read_inverted_list(std::ifstream &input_stream, uint64_t n){
    uint64_t x;
    uint64_t f;
    uint64_t i;

    // int_vector<>* inverted_list  = new int_vector<>(n);
    vector<uint32_t>* inverted_list = new vector<uint32_t>;
    for (i = 0; i < n; i++){
        input_stream >> x;
        input_stream >> f;
        // (*inverted_list)[i] = x;
        inverted_list -> push_back(x);
    }

    return inverted_list;
}


void performQueryLog(string query_log_path, string ii_path) {
    
    std::ifstream query_stream(query_log_path);
    std::ifstream ii_stream(ii_path);

    if (!query_stream.is_open()) {
        cout << "Can't open queries file: " << query_log_path << endl;
        return;
    }

    if (!ii_stream.is_open()) {
        cout << "Can't open inverted index file: " << ii_path << endl;
        return;
    }
    // Get all terms of queries
    std::vector<uint64_t> all_termsId = vector<uint64_t>(std::istream_iterator<uint64_t>(query_stream), std::istream_iterator<uint64_t>() );
    query_stream.close();

    cout << "-> Total de terms id en querys (con duplicados): " << all_termsId.size() << endl;
    std::sort(all_termsId.begin(), all_termsId.end());
    all_termsId.erase( unique( all_termsId.begin(), all_termsId.end() ), all_termsId.end() );
    cout << "-> Numero total de terms id (sin duplicar): " << all_termsId.size() << endl;

    // Indexing inverted lists
    map<uint64_t, vector<uint32_t>> il_vectors;
    map<uint64_t, s_sequence> ss_sequences;
    
    uint64_t n_il = 0;
    while (!ii_stream.eof() && n_il < all_termsId.size()) {
        uint64_t termId;
        uint64_t n;

        ii_stream >> termId;
        ii_stream >> n;
        if (all_termsId[n_il] == termId) {
            vector<uint32_t> *il = read_inverted_list(ii_stream, n);
            il_vectors.insert(std::pair<uint64_t, vector<uint32_t>>(termId, *il));
            
            s_sequence::builder builder;
            auto stats = builder.build(il->data(), il->size());
            s_sequence ss(builder.data());
            ss_sequences.insert(std::pair<uint64_t, s_sequence>(termId, ss));
            // delete il;
            n_il++;
        }
        else{
            ii_stream.ignore(numeric_limits<streamsize>::max(), '\n');
        }
    }

    cout << "-> End indexing inverted lists" << endl;

    cout << "-> Start processing queries" << endl;

    // Procesing queries
    std::ifstream query_log_stream(query_log_path);
    if (!query_log_stream.is_open()) {
        cout << "Can't open queries file: " << query_log_path << endl;
        return;
    }

    std::string line;
    // uint64_t max_number_of_sets = 0;
    uint64_t number_of_queries = 0;
    uint64_t total_time = 0;
    // uint64_t total_time_bk = 0;
    while ( getline( query_log_stream, line ) ) {
        vector <vector<uint32_t>> sets;
        vector <s_sequence> sliced_sequences;
        std::istringstream is( line );
        vector <uint64_t> termsId = std::vector<uint64_t>( std::istream_iterator<int>(is), std::istream_iterator<int>());
        // if (termsId.size() <= 16 && termsId.size() > 1) {
        if (termsId.size() <= 2 && termsId.size() > 1) {
            for (uint16_t i = 0; i < termsId.size(); ++i){
                sets.push_back(il_vectors[termsId[i]]);
                sliced_sequences.push_back(ss_sequences[termsId[i]]);
            }

            auto start = std::chrono::high_resolution_clock::now();
            uint64_t time;
            std::vector<uint32_t> out;
            pairwise_intersection(sliced_sequences[0], sliced_sequences[1], out.data());
            auto end = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            total_time += elapsed.count();
            total_time += time;
            cout << "i: " << number_of_queries << " |n: " << termsId.size() << " |Time execution: " << (float)time*10e-6 << "[ms]" << endl;
 
            number_of_queries++;
        }
        // if (termsId.size() > max_number_of_sets) {
        //     max_number_of_sets = termsId.size();
        // }                                              
        // cout << "Number of sets: " << termsId.size() << " ";
        // for (uint16_t i= 0; i < termsId.size(); ++i) {
        //     cout << termsId[i] << " ";
        // }
        // cout << endl;
        // number_of_queries++;
    }
    cout << "---------------------------------------" << endl;
    // cout << "Número maximo de conjuntos por query: " << max_number_of_sets << endl;
    cout << "Número total de queries: " << number_of_queries << endl;
    cout << "Tiempo promedio:" << (float)(total_time*10e-6)/number_of_queries << "[ms]" << endl;
    // cout << "Tiempo promedio B&K: " << (float)(total_time_bk*10e-6)/number_of_queries << "[ms]" << endl;


    query_log_stream.close();
    ii_stream.close();

}

int main(int argc, char** argv) {
    if (argc == 3) {
        std::string ii_path = argv[1];
        std::string query_path = argv[2];
        
        performQueryLog(query_path, ii_path);
        return 0;
    }
    else {
        return 1;
    }
    
}

// int main(int argc, char** argv) {
//     int mandatory = 1;
//     char const* output_filename = nullptr;

//     for (int i = mandatory; i != argc; ++i) {
//         if (std::string(argv[i]) == "-o") {
//             ++i;
//             output_filename = argv[i];
//         } else if (std::string(argv[i]) == "-h") {
//             std::cout << argv[0] << " -o output_filename < input" << std::endl;
//             return 1;
//         } else {
//             std::cout << "unknown option '" << argv[i] << "'" << std::endl;
//             return 1;
//         }
//     }

//     std::vector<uint32_t> input;

//     {  // read input from std::in
//         uint32_t n, x;
//         std::cin >> n;
//         input.reserve(n);
//         for (uint32_t i = 0; i != n; ++i) {
//             std::cin >> x;
//             input.push_back(x);
//         }
//     }

//     // build the sequence and print statistics
//     s_sequence::builder builder;
//     auto stats = builder.build(input.data(), input.size());
//     stats.print();

//     mm::file_source<uint8_t> mm_file;
//     uint8_t const* data = nullptr;

//     if (output_filename) {  // if an output file is specified, then serialize
//         essentials::print_size(builder);
//         essentials::save<s_sequence::builder>(builder, output_filename);

//         // mmap
//         int advice = mm::advice::normal;  // can be also random and sequential
//         mm_file.open(output_filename, advice);

//         // skip first 8 bytes storing the number of written bytes
//         data = mm_file.data() + 8;

//     } else {  // otherwise work directly in memory
//         data = builder.data();
//     }

//     // initialize a s_sequence from data, regardless the source
//     s_sequence ss(data);

//     uint32_t size = ss.size();

//     // decode whole list to an output buffer
//     std::vector<uint32_t> out(size);
//     ss.decode(out.data());
//     // check written values
//     uint32_t value = 0;
//     for (uint32_t i = 0; i != size; ++i) {
//         if (input[i] != out[i]) {
//             std::cout << "got " << out[i] << " but expected " << input[i]
//                       << std::endl;
//             return 1;
//         }

//         ss.select(i, value);  // select i-th element
//         if (value != out[i]) {
//             std::cout << "got " << value << " but expected " << out[i]
//                       << std::endl;
//             return 1;
//         }
//     }

//     return 0;
// }

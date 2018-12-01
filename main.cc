#include "cfg.h"

#include <vector>
#include <iomanip>
#include <iostream>
#include <fstream>

#include <unistd.h>

static std::vector<uint8_t> read_binary(const char* path)
{
    std::ifstream input(path, std::ios::in|std::ios::binary);

    input.seekg(0, std::ios::end);
    std::streampos size = input.tellg();
    input.seekg(0, std::ios::beg);

    std::vector<uint8_t> data;
    data.resize(size);
    
    input.read((char*)data.data(), size);
    return data;
}

int main(int argc, char** argv)
{
    int opt;
    const char* output_path = nullptr;
    const char* input_path = nullptr;
    while ((opt = getopt(argc, argv, "i:o:")) != -1) {
        switch(opt) {
        case 'i':
            input_path = optarg;
            break;
        case 'o':
            output_path = optarg;
            break;
        default:
            return EXIT_FAILURE;
        };
    }

    if (!input_path) {
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> code = read_binary(input_path);
    std::shared_ptr<ControlFlowGraph> cfg = ControlFlowGraph::create(code.data(), code.size(), 0x0ull);
    if (!cfg) {
        exit(EXIT_FAILURE);
    }

    cfg->visit([](const cfg_node* node) {
        const cfg_basic_block* bb = static_cast<const cfg_basic_block*>(node);
        fprintf(stderr, "> addr: 0x%llx\n", (unsigned long long)bb->addr);
        fprintf(stderr, "> size: %llu\n", (unsigned long long)bb->size);
        fprintf(stderr, "> instruction count: %llu\n", (unsigned long long)bb->insn_count);

        for (uint32_t i = 0; i < bb->insn_count; ++i) {
            fprintf(stderr, "0x%llx:\t%s\t\t%s\n",
                (unsigned long long) bb->insn[i].address,
                bb->insn[i].mnemonic,
                bb->insn[i].op_str);
        }

        fprintf(stderr, "\n");
    });

    if (output_path) {
        std::ofstream file(output_path, std::ios::out);
        file << cfg->generate_dot();
        file.close();
    }

    return 0;
}


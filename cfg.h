#pragma once

#include <map>
#include <vector>
#include <functional>
#include <sstream>
#include <memory>

#include <capstone/capstone.h>

struct cfg_node;
struct cfg_edge;
struct cfg_basic_block;

/* Generic edge description */
struct cfg_edge
{
    /* This edge describes a branch (normal control transfer otherwise) */
    bool is_branch;

    /* This edge describes a taken branch */
    bool is_taken;

    /* Edge target */
    cfg_node* to;
};

/* Generic CFG node */
struct cfg_node
{
    std::vector<cfg_edge> edges;

    /* Visitor sequence number, used in cfg traversal */
    uint32_t visitor_seq = 0;
};

/* Disassembled basic block node */
struct cfg_basic_block : cfg_node
{
    /* Basic block base address */
    uint64_t addr;

    /* Block size in bytes */
    uint64_t size;

    /* Block instructions */
    cs_insn* insn;

    /* Total instructions in block */
    uint32_t insn_count;
};

/**
 * Control flow graph class
 */
class ControlFlowGraph
{
public:

    /**
     * Create and parse CFG from raw binary data
     */
    static std::shared_ptr<ControlFlowGraph> create(const void* data, size_t size, uintptr_t baseaddr);

    /**
     * Visit CFG in depth-first order
     */
    void visit(std::function<void(const cfg_node*)> visitor);

    /**
     * Generate graph in DOT format
     */
    std::string generate_dot();

    ControlFlowGraph();
    ~ControlFlowGraph();

protected:

    /** Parse CFG from instructions */
    bool parse(cs_insn* insn_buffer, size_t insn_count, uint64_t baseaddr);

    /** Begin visiting CFG */
    void begin() {
        m_seq = m_entry->visitor_seq + 1;
    }

    /** Check if node was visited in this sequence */
    bool is_visited(cfg_node* node) {
        return node->visitor_seq == m_seq;
    }

    /** Mark node as visited */
    void mark_visited(cfg_node* node) {
        node->visitor_seq = m_seq;
    }

    /** Visit specific node in depth-first order */
    void visit_node(struct cfg_node* node, std::function<void(const cfg_node*)> visitor);

    /** Create and add new basic block node */
    cfg_basic_block* add_basic_block(
        uint64_t addr,
        uint64_t size,
        cs_insn* insn,
        uint32_t insn_count);

    /** Find basic block that contains an address */
    cfg_basic_block* find_basic_block(uint64_t addr);

private:

    /* Parsed instructions */
    cs_insn* m_insn_buffer;
    size_t m_insn_count;

    /* CFG entry */
    cfg_node* m_entry;

    /* Basic block lookup cache keyed by block start address */
    std::map<uint64_t, cfg_basic_block*> m_blocks;

    /* Traversal sequence */
    uint64_t m_seq;
};


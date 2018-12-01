#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include <list>
#include <iomanip>
#include <iostream>
#include <fstream>

#include "cfg.h"

////////////////////////////////////////////////////////////////////////////////

static void create_edge(cfg_basic_block* src, cfg_basic_block* dst, bool is_branch, bool is_taken)
{
    cfg_edge edge;
    edge.is_branch = is_branch;
    edge.is_taken = is_taken;
    edge.to = dst;

    src->edges.push_back(edge);
}

static void set_branch_target(cfg_basic_block* src, cfg_basic_block* dst, bool brtaken)
{
    create_edge(src, dst, true, brtaken);
}

static void set_normal_target(cfg_basic_block* src, cfg_basic_block* dst)
{
    create_edge(src, dst, false, false);
}

/* Returns true if instructuin is in control flow group */
static bool is_control_flow_insn(const cs_insn* insn)
{
    for (size_t i = 0; i < insn->detail->groups_count; ++i) {
        if (insn->detail->groups[i] == CS_GRP_JUMP) {
            return true;
        }
    }

    return false;
}

static bool is_unconditional_jump(const cs_insn* insn)
{
    return (insn->id == X86_INS_JMP);
}

/* Figure out jump target from jmp instruction */
static uint64_t get_jump_target(const cs_insn* insn)
{
    cs_x86* x86_insn = &insn->detail->x86;
    assert(x86_insn->op_count == 1);

    cs_x86_op* op = x86_insn->operands;
    
    switch (op->type) {
    case X86_OP_REG:
    case X86_OP_MEM:
        /* don't know how to handle indirect jumps */
        return 0;

    case X86_OP_IMM:
        return op->imm;

    default:
        assert(0);
        return 0;
    };
}

ControlFlowGraph::ControlFlowGraph()
    : m_insn_buffer(nullptr),
      m_insn_count(0),
      m_entry(nullptr),
      m_seq(0)
{
}

ControlFlowGraph::~ControlFlowGraph()
{
    if (!m_insn_buffer) {
        return;
    }

    cs_free(m_insn_buffer, m_insn_count);
    for (auto i = m_blocks.begin(); i != m_blocks.end(); ++i) {
        delete i->second;
    }
}

cfg_basic_block* ControlFlowGraph::add_basic_block(
    uint64_t addr,
    uint64_t size,
    cs_insn* insn,
    uint32_t insn_count)
{
    cfg_basic_block *bb = new cfg_basic_block;
    bb->addr = addr;
    bb->size = size;
    bb->insn = insn;
    bb->insn_count = insn_count;

    m_blocks[addr] = bb;

    return bb;
}

cfg_basic_block* ControlFlowGraph::find_basic_block(uint64_t addr)
{
    auto i = m_blocks.lower_bound(addr);
    if (i->first == addr) {
        return i->second;
    } else if (i == m_blocks.begin()) {
        return NULL;
    } else {
        return std::prev(i)->second;
    }
}

std::shared_ptr<ControlFlowGraph> ControlFlowGraph::create(const void* data, size_t size, uintptr_t baseaddr)
{
    if (!data || size == 0) {
        return nullptr;
    }

    csh cs_handle;
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &cs_handle) != CS_ERR_OK) {
        return nullptr;
    }

    cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);

    cs_insn* insn_buffer = nullptr;
    size_t insn_count = cs_disasm(cs_handle, (const uint8_t*)data, size, baseaddr, 0, &insn_buffer);
    if (insn_count <= 0) {
        return nullptr;
    }

    cs_close(&cs_handle);

    auto cfg = std::make_shared<ControlFlowGraph>();
    if (!cfg->parse(insn_buffer, insn_count, baseaddr)) {
        cs_free(insn_buffer, insn_count);
        return nullptr;
    }

    return cfg;
}

bool ControlFlowGraph::parse(cs_insn* insn_buffer, size_t insn_count, uint64_t baseaddr)
{
    //
    // Find linear basic blocks
    //

    uint64_t block_start = baseaddr;
    uint64_t block_size = 0;
    uint32_t block_insn_count = 0;
    std::vector<cs_insn*> jumps;
    cfg_basic_block* prev = NULL;

    for (size_t i = 0; i < insn_count; ++i) {
        cs_insn *insn = insn_buffer + i;

        block_size += insn->size;
        block_insn_count++;

        if (is_control_flow_insn(insn)) {
            cfg_basic_block* bb = add_basic_block(
                block_start,
                block_size,
                insn - block_insn_count + 1,
                block_insn_count);

            if (prev && !is_unconditional_jump(jumps.back())) {
                set_branch_target(prev, bb, false);
            }

            jumps.push_back(insn);

            block_start += block_size;
            block_size = 0;
            block_insn_count = 0;
            prev = bb;
        }
    }

    if (block_size > 0) {
        cfg_basic_block* bb = add_basic_block(
            block_start,
            block_size,
            insn_buffer + insn_count - block_insn_count,
            block_insn_count);

        if (prev && !is_unconditional_jump(jumps.back())) {
            set_branch_target(prev, bb, false);
        }
    }

    //
    // Handle jumps 
    //
    
    for (cs_insn* jump_insn : jumps) {
        uint64_t target_addr = get_jump_target(jump_insn);
        if (!target_addr) {
            continue;
        }

        cfg_basic_block* src_bb = find_basic_block(jump_insn->address);
        cfg_basic_block* target_bb = find_basic_block(target_addr);
        if (!target_bb) {
            /* We haven't seen this jump target
             * We will have to parse this as an unknown block later */
            return false;
        }

        if (target_bb->addr == target_addr) {
            /* Jump target aliases existing block start */
            set_branch_target(src_bb, target_bb, true);
            continue;
        }

        /* Find instruction within target basic block that is targeted by this jump */
        cs_insn* target_insn = target_bb->insn + 1;
        uint32_t lead_insn_count = 1;
        while (target_insn->address < target_addr) {
            ++target_insn;
            ++lead_insn_count;
        }

        if (target_insn->address != target_addr) {
            /* Inter-opcode jump, we don't support that */
            return false;
        }

        /* Split target block in two */
        cfg_basic_block* tail_bb = add_basic_block(
            target_addr,
            target_bb->addr + target_bb->size - target_addr,
            target_insn,
            target_bb->insn_count - lead_insn_count);

        /* Correct original block size */
        target_bb->size = target_addr - target_bb->addr;
        target_bb->insn_count = lead_insn_count;

        /* New tail block should inherit all previous exits */
        tail_bb->edges = std::move(target_bb->edges);

        /* Connect branch source and target blocks */
        set_branch_target(src_bb, tail_bb, true);

        /* Connect split blocks */
        set_normal_target(target_bb, tail_bb);
    }
    
    //
    // Return block with the least address as an entry point
    //
    
    m_insn_buffer = insn_buffer;
    m_insn_count = insn_count;
    m_entry = find_basic_block(baseaddr);

    return true;
}

void ControlFlowGraph::visit_node(cfg_node* node, std::function<void(const cfg_node*)> visitor)
{
    if (!node) {
        return;
    }

    visitor(node);

    /* Mark node as visited */
    mark_visited(node);

    /* Depth-first traversal */
    cfg_basic_block* bb = static_cast<cfg_basic_block*>(node);
    for (auto edge : bb->edges) {
        if (is_visited(edge.to)) {
            continue;
        }

        visit_node(edge.to, visitor);
    }
}

void ControlFlowGraph::visit(std::function<void(const cfg_node*)> visitor)
{
    begin();
    visit_node(m_entry, visitor);
}

static std::string dot_node_name(const cfg_node* node)
{
    std::ostringstream buf;
    buf << std::hex << "\"" << "0x" << static_cast<const cfg_basic_block*>(node)->addr << "\"";
    return buf.str();
}

static std::string dot_format_edge(const cfg_node* node, const cfg_edge* edge)
{
    std::ostringstream buf;

    buf << dot_node_name(node) << "->" << dot_node_name(edge->to)
        << (edge->is_branch ?
                edge->is_taken ?
                    " [color=blue]" :
                    " [color=red]" :
                " [color=gray] ")
        << std::endl;

    return buf.str();
}

static std::string dot_format_node(const cfg_node* node)
{
    std::stringstream strbuf;
    strbuf << dot_node_name(node)
           << " [shape=record fontname=courier pin=true label=\"";

    const cfg_basic_block* bb = static_cast<const cfg_basic_block*>(node);
    for (uint32_t i = 0; i < bb->insn_count; ++i) {
        strbuf << "0x" << std::hex << std::setfill('0') << std::setw(8) << bb->insn[i].address << ": "
               << bb->insn[i].mnemonic << " "
               << bb->insn[i].op_str
               << "\\l";
    }

    strbuf << "\"]" << std::endl;

    return strbuf.str();
}

std::string ControlFlowGraph::generate_dot()
{
    std::ostringstream strbuf;
    strbuf << "digraph \"disassembly\" {" << std::endl
           << "rank=same" << std::endl
           << "rankdir=TB" << std::endl
           << "rank1 [style=invis]" << std::endl; 

    /* Walk basic blocks in address order and form fake connections between them.
     * This hack allows us to draw nodes in address order. */
    strbuf << "rank1";
    for (auto i = m_blocks.begin(); i != m_blocks.end(); ++i) {
        strbuf << "->" << dot_node_name(i->second);
    }
    strbuf << " [style=invis]" << std::endl;

    /* Walk CFG normally */
    this->visit([&](const cfg_node* node) {
        strbuf << dot_format_node(node);
        for (auto edge : node->edges) {
            strbuf << dot_format_edge(node, &edge);
        }
    });

    strbuf << "}";

    return strbuf.str();
}

////////////////////////////////////////////////////////////////////////////////


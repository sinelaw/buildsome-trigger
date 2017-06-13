#ifndef __FS_TREE_H__
#define __FS_TREE_H__

extern "C" {
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
}

#include <leveldb/db.h>
#include "typed_db.h"


struct FileName {
    char file_name[1024];
};

struct ResultKey {
    uint64_t result_key;
};

struct Time {
    uint64_t time;
};

enum class BranchType {
    BranchTypeNext,
    BranchTypeResult,
};

enum class FileStateType {
    FileReadable,
    FileInaccessible,
};

struct FileState {
    struct {
        mode_t mode;
        off_t size;
    } stat;
    Hash hash;
};

struct InputState {
    enum FileStateType file_state_type;
    union {
        FileState state_readable;
        // inaccessible? errno?
    };
    Time last_usage;
    enum BranchType next_branch_type;
    union {
        FileName next;
        ResultKey result;
    };
};

// InputKey = (parent, index)
// e.g.
// (CommandKey, index) - for top-level inputs (first inputs)
// (InputKey, index) - for next levels
//
// Also we can maintain a table:
// filename -> parent A, parent B, etc.
// The table itself is keyed by: filename
struct Input {
public:
    FileName name;
    InputState state;
    Input(const FileName &name, const InputState &state)
        : name(name), state(state) { }
};

struct Command {
    char command_line[1024];
};

struct Output {
    FileState state;
    FileName name;
};

enum class OutcomeType {
    OutcomeTypeOutputsCreated,
        OutcomeTypeFailure,
        };

struct Outcome {
    enum OutcomeType result_type;
    union {
        struct {
            uint32_t outputs_count;
            Output outputs[64];
        } outputs_created;
        struct {
            int exit_code;
        } failure;
    };
};

class FSTree {
private:
    TypedDB m_db;

public:
    FSTree();

    enum class NodeType {
        NodeTypeInput,
        NodeTypeOutcome,
    };

    struct Node {
    public:
        Node(const Input &input)     : type(FSTree::NodeType::NodeTypeInput),   input(input)     { }
        Node(const Outcome &outcome) : type(FSTree::NodeType::NodeTypeOutcome), outcome(outcome) { }
        Node(const Node &other);

        bool operator==(const Node &other) const;

        enum NodeType type;
        union {
            Input input;
            Outcome outcome;
        };
    };

    class CommandKey : public Key<Command> {
    private:
        Hash m_hash;
    public:
        CommandKey(const Command &x);
        const Hash *get_hash() const override { return &m_hash; }
    };

    class NodeKey : public Key<Node> {
    private:
        Hash m_hash;
    public:
        NodeKey(const CommandKey &parent, uint32_t idx);
        NodeKey(const NodeKey &parent, uint32_t idx);
        const Hash *get_hash() const override { return &m_hash; }
    };

    /* Main question to ask this database: should I run this command
     * or are all outputs up to date already given the current inputs? */
    Optional<FSTree::Node> try_lookup_root(const FSTree::CommandKey &, uint32_t idx) const;
    Optional<FSTree::Node> try_lookup_child(const FSTree::NodeKey &, uint32_t idx) const;

    void add_root(const FSTree::CommandKey &, const FSTree::Node &root);
    void add_child(const FSTree::NodeKey &, const FSTree::Node &child);

};

bool is_up_to_date(const FSTree &, const Command &);

#endif

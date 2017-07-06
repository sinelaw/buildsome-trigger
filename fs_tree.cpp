#include "fs_tree.h"
#include "typed_db.h"
#include "assert.h"

#include <iostream>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <bsd/md5.h>
}

FSTree::CommandKey::CommandKey(const Command &x) {
    calc_hash(&x, &this->m_hash);
}

FSTree::NodeKey::NodeKey(const CommandKey &parent, uint32_t idx) {
    this->m_hash = *parent.get_hash();
    static_assert(sizeof(this->m_hash) >= sizeof(idx));
    *(decltype(idx)*)&this->m_hash += idx;
}

FSTree::NodeKey::NodeKey(const FSTree::NodeKey &parent, uint32_t idx) {
    this->m_hash = *parent.get_hash();
    static_assert(sizeof(this->m_hash) >= sizeof(idx));
    *(decltype(idx)*)&this->m_hash += idx;
}

FSTree::Node::Node(const FSTree::Node &other) {
    switch (other.type) {
    case FSTree::NodeType::NodeTypeInput: this->input = other.input; break;
    case FSTree::NodeType::NodeTypeOutcome: this->outcome = other.outcome; break;
    default: assert(false);
    }
    this->type = other.type;
}

bool FSTree::Node::operator==(const FSTree::Node &other) const {
    static_assert(std::is_standard_layout<Input>::value);
    static_assert(std::is_standard_layout<Outcome>::value);
    if (this->type != other.type) return false;
    switch (this->type) {
    case FSTree::NodeType::NodeTypeInput: return (!memcmp(&this->input, &other.input, sizeof(other.input)));
    case FSTree::NodeType::NodeTypeOutcome: return (!memcmp(&this->outcome, &other.outcome, sizeof(other.outcome)));
    default: assert(false);
    }
}

FSTree::FSTree() { }

Optional<FSTree::Node> FSTree::try_lookup_root(const FSTree::CommandKey &cmd_key, uint32_t idx) const
{
    const NodeKey key(cmd_key, idx);
    return m_db.TryGet(key);
}

Optional<FSTree::Node> FSTree::try_lookup_child(const FSTree::NodeKey &parent_key, uint32_t idx) const
{
    const NodeKey key(parent_key, idx);
    return m_db.TryGet(key);
}

void FSTree::add_root(const FSTree::CommandKey &cmd_key, const FSTree::Node &root)
{
    for (uint32_t i = 0; ; i++) {
        Optional<FSTree::Node> o_other = this->try_lookup_root(cmd_key, i);
        if (!o_other.has_value()) {
            const NodeKey key(cmd_key, i);
            m_db.Put(key, &root);
            return;
        }
        if (o_other.get_value() == root) return;
    }
}

void FSTree::add_child(const FSTree::NodeKey &parent_key, const FSTree::Node &child)
{
    for (uint32_t i = 0; ; i++) {
        Optional<FSTree::Node> o_other = this->try_lookup_child(parent_key, i);
        if (!o_other.has_value()) {
            const NodeKey key(parent_key, i);
            m_db.Put(key, &child);
            return;
        }
        if (o_other.get_value() == child) return;
    }
}

static bool check_input(const Input &input)
{
    struct stat stat_buf;
    const int res = stat(input.name.file_name, &stat_buf);
    if (res != 0) {
        if ((errno == ENOENT)
            || (errno == EACCES)
            || (errno == ENOTDIR)
            || (errno == ENAMETOOLONG)
            || (errno == ELOOP))
        {
            switch (input.state.file_state_type) {
            case FileStateType::FileInaccessible: return true;
            case FileStateType::FileReadable: return false;
            default: assert(0);
            }
        }
        assert(0); // problem with stat
    }
    // stat ok
    switch (input.state.file_state_type) {
    case FileStateType::FileInaccessible: return false;
    case FileStateType::FileReadable: break;
    default: assert(0);
    }
    const FileState &state = input.state.state_readable;
    if ((stat_buf.st_mode != state.stat.mode)
        || (stat_buf.st_size != state.stat.size))
    {
        return false;
    }
    char md5[MD5_DIGEST_STRING_LENGTH];
    const char *const md5_res = MD5File(input.name.file_name, md5);
    assert(md5_res);
    static_assert(sizeof(md5) <= sizeof(state.hash.hash));
    return 0 == memcmp(md5, state.hash.hash, sizeof(md5));
}

static Optional<Outcome> try_get_outcome_by_input(const FSTree &db, const FSTree::NodeKey &parent_key,
                                                  const Input &input)
{
    if (!check_input(input)) return Optional<Outcome>();
    for (uint32_t i = 0; ; i++) {
        const Optional<FSTree::Node> o_child = db.try_lookup_child(parent_key, i);
        if (!o_child.has_value()) break;
        const FSTree::Node &child = o_child.get_value();
        switch (child.type) {
        case FSTree::NodeType::NodeTypeInput: {
            const Optional<Outcome> res = try_get_outcome_by_input(db, FSTree::NodeKey(parent_key, i), child.input);
            if (res.has_value()) return res;
            continue;
        }
        case FSTree::NodeType::NodeTypeOutcome:
            return Optional<Outcome>(child.outcome);
        default: assert(0);
        }
    }
    return Optional<Outcome>();
}

Optional<Outcome> try_get_outcome(const FSTree &db, const Command &cmd)
{
    FSTree::CommandKey key(cmd);
    for (uint32_t i = 0; ; i++) {
        const Optional<FSTree::Node> o_root = db.try_lookup_root(key, i);
        if (!o_root.has_value()) break;
        const FSTree::Node &root = o_root.get_value();
        switch (root.type) {
        case FSTree::NodeType::NodeTypeInput: {
            const Optional<Outcome> res = try_get_outcome_by_input(db, FSTree::NodeKey(key, i), root.input);
            if (res.has_value()) return res;
            continue;
        }
        case FSTree::NodeType::NodeTypeOutcome:
            return Optional<Outcome>(root.outcome);
        default: assert(0);
        }
    }
    return Optional<Outcome>();
}


int main(int argc, const char *const *argv)
{
    FSTree db;
    Command cmd;
    ASSERT(argc > 1, "Usage: " << argv[0] << " <string>");
    strncpy(cmd.command_line, argv[1], sizeof(cmd.command_line));
    const Optional<Outcome> outcome = try_get_outcome(db, cmd);
    if (outcome.has_value()) {
        std::cout << "Found it!" << std::endl;
    }
    return 0;
}

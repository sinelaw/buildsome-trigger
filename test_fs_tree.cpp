#include "fs_tree.h"


int main(int argc, const char *const *argv)
{
    FSTree db;
    Command cmd;
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <string>" << std::endl;
        return 1;
    }
    strncpy(cmd.command_line, argv[1], sizeof(cmd.command_line));
    const Optional<Outcome> outcome = try_get_outcome(db, cmd);
    if (outcome.has_value()) {
        std::cout << "Found it!" << std::endl;
        return 0;
    }

    return 0;
}

#include "trigger.h"

#include <cstdlib>
#include <iostream>

extern "C" {
#include "fshook/protocol.h"
}

#define DEFINE_DATA(type, buf, buf_size, name)  \
    ASSERT(buf_size == sizeof(type));           \
    type *name __attribute__((unused)) = (type *)buf;

void file_request(enum func func, const char *buf, uint32_t buf_size) {
    // std::cout << "HANDLING: " << buf << std::endl;
    switch (func) {
    case func_openw: {
        DEFINE_DATA(struct func_openw, buf, buf_size, data);
        break;
    }
    case func_creat: {
        DEFINE_DATA(struct func_creat, buf, buf_size, data);
        break;
    }
    case func_truncate: {
        DEFINE_DATA(struct func_truncate, buf, buf_size, data);
        break;
    }
    case func_unlink: {
        DEFINE_DATA(struct func_unlink, buf, buf_size, data);
        break;
    }
    case func_rename: {
        DEFINE_DATA(struct func_rename, buf, buf_size, data);
        break;
    }
    case func_chmod: {
        DEFINE_DATA(struct func_chmod, buf, buf_size, data);
        break;
    }
    case func_mknod: {
        DEFINE_DATA(struct func_mknod, buf, buf_size, data);
        break;
    }
    case func_mkdir: {
        DEFINE_DATA(struct func_mkdir, buf, buf_size, data);
        break;
    }
    case func_rmdir: {
        DEFINE_DATA(struct func_rmdir, buf, buf_size, data);
        break;
    }
    case func_link: {
        DEFINE_DATA(struct func_link, buf, buf_size, data);
        break;
    }
    case func_chown: {
        DEFINE_DATA(struct func_chown, buf, buf_size, data);
        break;
    }

    case func_openr: {
        DEFINE_DATA(struct func_openr, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_stat: {
        DEFINE_DATA(struct func_stat, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_lstat: {
        DEFINE_DATA(struct func_lstat, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_opendir: {
        DEFINE_DATA(struct func_opendir, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_access: {
        DEFINE_DATA(struct func_access, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_readlink: {
        DEFINE_DATA(struct func_readlink, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_symlink: {
        DEFINE_DATA(struct func_symlink, buf, buf_size, data);
        std::cout << "PATH: '" << data->target.in_path << "'" << std::endl;
        break;
    }
    case func_exec: {
        DEFINE_DATA(struct func_exec, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }
    case func_realpath: {
        DEFINE_DATA(struct func_realpath, buf, buf_size, data);
        std::cout << "PATH: '" << data->path.in_path << "'" << std::endl;
        break;
    }

    case func_execp: {
        DEFINE_DATA(struct func_execp, buf, buf_size, data);
        std::cout << "PATH: '" << data->file << "'" << std::endl;
        break;
    }
    case func_trace: {
        DEFINE_DATA(struct func_trace, buf, buf_size, data);
        std::cout << "TRACE: '" << data->msg << "'" << std::endl;
        break;
    }
    default: PANIC("Bad enum: %u", func);
    }
}

int main(int argc, char *const argv[]) {

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " COMMAND..." << std::endl;
        exit(1);
    }

    Trigger trigger(&file_request);
    trigger.Execute(argv[1], (argv + 1));
}

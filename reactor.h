#pragma once

#include <map>
#include <utility>
#include <vector>

class Reactor {
public:

    enum Event {
        Accept,
        Read,
        Write,
    };

    typedef void Handler(int fd, enum Event);

    Reactor();

    void Register(int fd, enum Event, Handler *func);

private:

    struct HandlerItem {
        int fd;
        enum Event event;
        Handler *handler;
    };


    std::vector< HandlerItem > m_fds;

    void Poll();
};

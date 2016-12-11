#include "reactor.h"

#include <utility>

Reactor::Reactor() { }

void Reactor::Register(int fd, enum Event event, Handler *func)
{

    struct HandlerItem item = {
        .fd = fd,
        .event = event,
        .handler = func,
    };
    m_fds.push_back(item);

    // TODO Add to epoll ?
}

void Reactor::Poll()
{
    // TODO epoll_wait() ?
}

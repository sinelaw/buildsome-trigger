.PHONY: default clean

default: fs_override.so trigger fs_tree
check-syntax: default
clean:
	rm -f *.o *.so fs_tree trigger

WARNINGS=-Wswitch-enum -Wall -Werror -Wextra
GPP=g++ -no-pie
GCC=gcc -no-pie
CLANGPP=clang++

CXX=${GPP} -g ${WARNINGS} -std=c++11 -pthread  -msse4.2
CC=${GCC} -g ${WARNINGS} -std=gnu11

trigger: main.cpp trigger.o ThreadPool.h
	${CXX} main.cpp trigger.o -o "$@"

trigger.o: trigger.cpp trigger.h ThreadPool.h
	${CXX} -c "$<" -o "$@"

fs_override.so: fshook/*.c fshook/*.h
	${CC} -o "$@" -Winit-self -shared -fPIC -D_GNU_SOURCE fshook/*.c -ldl

fs_tree.o: fs_tree.cpp fs_tree.h typed_db.h
	${CXX} -c "$<" -o "$@"

fs_tree: fs_tree.o
	${CXX} $^ -lbsd -lleveldb -o "$@"

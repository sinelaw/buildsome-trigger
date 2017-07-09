.PHONY: default clean

default: fs_override.so test_fs_tree test_build_rules
check-syntax: default
clean:
	rm -f *.o *.so fs_tree trigger

WARNINGS=-Wswitch-enum -Wall -Werror -Wextra
GPP=g++ -no-pie
GCC=gcc -no-pie
CLANGPP=clang++

CXX=${GPP} -g ${WARNINGS} -std=c++11 -pthread  -msse4.2
CC=${GCC} -g ${WARNINGS} -std=gnu11

fs_override.so: fshook/*.c fshook/*.h
	${CC} -o "$@" -Winit-self -shared -fPIC -D_GNU_SOURCE fshook/*.c -ldl

fs_tree.o: fs_tree.cpp fs_tree.h typed_db.h
	${CXX} -c "$<" -o "$@"

build_rules.o: build_rules.cpp build_rules.h
	${CXX} -c "$<" -o "$@"

test_fs_tree: test_fs_tree.cpp fs_tree.o
	${CXX} $^ -lbsd -lleveldb -o "$@"

test_build_rules: test_build_rules.cpp build_rules.o
	${CXX} $^  -o "$@"

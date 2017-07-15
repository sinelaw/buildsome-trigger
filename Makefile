.PHONY: default clean

default: out/fs_override.so out/test_fs_tree out/test_build_rules out/main
check-syntax: default
clean:
	rm -f out/*

WARNINGS=-Wswitch-enum -Wall -Werror -Wextra
GPP=g++ -no-pie
GCC=gcc -no-pie
CLANGPP=clang++

CXX=${CLANGPP} -g ${WARNINGS} -std=c++11 -pthread  -msse4.2
CC=${GCC} -g ${WARNINGS} -std=gnu11

out:
	mkdir "$@"

out/%.o: %.cpp *.h
	${CXX} -c "$<" -o "$@"

out/fs_override.so: fshook/*.c fshook/*.h out
	${CC} -o "$@" -Winit-self -shared -fPIC -D_GNU_SOURCE fshook/*.c -ldl

out/test_fs_tree: test_fs_tree.cpp out/fs_tree.o
	${CXX} $^ -lbsd -lleveldb -o "$@"

out/test_build_rules: test_build_rules.cpp out/build_rules.o
	${CXX} $^  -o "$@"

out/main: out/main.o out/build_rules.o out/job.o
	${CXX} $^ -lbsd -lleveldb -lstdc++ -o "$@"

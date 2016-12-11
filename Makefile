.PHONY: default clean

default: trigger.o fs_override.so trigger
clean:
	rm *.o trigger fs_override.so

WARNINGS=-Wswitch-enum -Wall -Werror -Wextra
GPP=g++ -no-pie
GCC=gcc -no-pie
CLANGPP=clang++

CXX=${GPP} -g ${WARNINGS} -std=c++11 -pthread -O3 -msse4.2
CC=${GCC} -g ${WARNINGS} -std=gnu11

trigger: main.cpp trigger.o
	${CXX} $^ -o "$@"

trigger.o: trigger.cpp trigger.h
	${CXX} -c "$<" -o "$@"

reactor.o: reactor.cpp reactor.h
	${CXX} -c "$<" -o "$@"

fs_override.so: fshook/*.c fshook/*.h
	${CC} -o "$@" -Winit-self -shared -fPIC -D_GNU_SOURCE fshook/*.c -ldl

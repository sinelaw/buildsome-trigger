.PHONY: default clean

default: trigger.o fs_override.so trigger
clean:
	rm *.o trigger fs_override.so

CXX=g++-5 -g3 -Wall -Werror -Wextra -std=c++11
CC=gcc -g3 -Wall -Werror -Wextra

trigger: main.cpp trigger.o
	${CXX} $^ -o "$@"

trigger.o: trigger.cpp trigger.h
	${CXX} -c "$<" -o "$@"

fs_override.so: fshook/*.c fshook/*.h
	${CC} -o "$@" -Winit-self -shared -fPIC -D_GNU_SOURCE fshook/*.c -ldl

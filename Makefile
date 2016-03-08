.PHONY: default clean

default: trigger.o
clean:
	rm *.o

CXX=g++-5 -Wall -Wextra

trigger.o: trigger.cpp trigger.h
	${CXX} -c "$<" -o "$@"

CXX=g++

all: dyntrace_threads

dyntrace_threads: dyntrace_threads.c
	$(CXX) -O2 -pthread -o dyntrace_threads dyntrace_threads.c

clean:
	rm -f dyntrace_threads

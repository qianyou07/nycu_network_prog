all: np_simple np_single_proc

np_simple: np_simple.cpp
	g++ -o np_simple np_simple.cpp -Wall

np_single_proc: np_single_proc.cpp
	g++ -o np_single_proc np_single_proc.cpp -std=c++11 -Wall

clean:
	rm np_simple
	rm np_single_proc
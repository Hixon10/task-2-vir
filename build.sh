#!/bin/bash

mkdir bin

g++ -std=c++11 aucont_start.cpp -o bin/aucont_start
g++ -std=c++11 aucont_stop.cpp -o bin/aucont_stop
g++ -std=c++11 aucont_list.cpp -o bin/aucont_list
g++ -std=c++11 aucont_exec.cpp -o bin/aucont_exec

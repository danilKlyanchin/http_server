#!/bin/bash
set -e

g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -pthread \
  -I"$(brew --prefix fmt)/include" \
  -L"$(brew --prefix fmt)/lib" \
  -o exec_server server.cpp -lfmt

echo "BUILD OK"

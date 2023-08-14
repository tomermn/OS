cmake_minimum_required(VERSION 3.16)
project(ex4)

set(CMAKE_CXX_STANDARD 11)

add_executable(ex4 VirtualMemory.cpp PhysicalMemory.cpp
        YaaraTest.cpp MemoryConstants.h VirtualMemory.h MemoryConstants.h )



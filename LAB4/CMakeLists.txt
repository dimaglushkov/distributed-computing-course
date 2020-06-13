cmake_minimum_required(VERSION 3.15)
project(LAB4 C)

include_directories(/home/allacee/ITMO/ORV/distributed-computing/LAB4/lib64)

set(CMAKE_C_STANDARD 99)
add_executable(LAB4 pa4.c ipc.c)
target_link_libraries(LAB4 /home/allacee/ITMO/ORV/distributed-computing/LAB4/lib64/libruntime.so)

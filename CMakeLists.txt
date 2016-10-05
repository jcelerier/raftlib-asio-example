cmake_minimum_required(VERSION 3.6)
project(RaftExample)

find_package(Boost REQUIRED COMPONENTS system)
add_executable(example ex.cpp)
target_include_directories(example PRIVATE RaftLib readerwriterqueue)
target_link_libraries(example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/raftlib-build/src/libraft.a pthread Boost::system )
set_target_properties(example PROPERTIES CXX_STANDARD 14)

cmake_minimum_required(VERSION 3.10)
project(GST_net)

# 生成 compile_commands.json，供 clangd / IDE 准确跳转与补全
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output)

# 下载 GST_log 依赖
include(FetchContent)
FetchContent_Declare(
    GST_log
    GIT_REPOSITORY https://github.com/Weizihan/GST-Log.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(GST_log)

# 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/EventLoop)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/Concurrency)

# 获取所有源文件
file(GLOB GST_RPC_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EventLoop/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Concurrency/*.cpp"
)

# 生成静态库 GST_net
add_library(GST_net STATIC ${GST_RPC_SRC})
target_link_libraries(GST_net PUBLIC Tlog)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

file(GLOB EXAMPLE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
add_executable(main ${EXAMPLE_SRC})
target_link_libraries(main PRIVATE GST_net)

add_executable(test_tcpclient tests/test_tcpclient.cpp)
target_link_libraries(test_tcpclient PRIVATE GST_net)

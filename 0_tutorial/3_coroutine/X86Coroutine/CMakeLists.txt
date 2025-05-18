cmake_minimum_required(VERSION 3.10)

# 项目信息
project(CoroutineProject LANGUAGES CXX)

# 设置编译器为32位
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")

# 添加源文件
set(SOURCES
    core.cpp
    coroutine.cpp
    fun.cpp
    main.cpp
)

# 添加头文件路径
include_directories(${CMAKE_SOURCE_DIR})

# 生成可执行文件
add_executable(CoroutineProject ${SOURCES})

# 链接必要的库（如果有）
# target_link_libraries(CoroutineProject <library_name>)
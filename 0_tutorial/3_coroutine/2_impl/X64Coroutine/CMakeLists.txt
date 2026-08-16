cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(MySimpleCoroutine)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加源文件
set(SOURCES
    main.cpp
    coroutine.cpp
)

# 添加头文件路径
include_directories(${CMAKE_SOURCE_DIR})

# 添加汇编文件
enable_language(ASM_NASM)
set(ASM_SOURCES
    JumpContext.asm
    MakeContext.asm
)

# 创建可执行文件
add_executable(MySimpleCoroutine ${SOURCES} ${ASM_SOURCES})

# 链接必要的库（如果有）
# target_link_libraries(MySimpleCoroutine <library_name>)
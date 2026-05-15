cmake_minimum_required(VERSION 3.10)
project(CppLearningProject)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

# 1. 找到 src 目录下的所有条目
file(GLOB SUBDIRS "src/*")

# 2. 遍历这些条目
foreach(DIR ${SUBDIRS})
    # 判断它是不是一个真正的文件夹 📁
    if(IS_DIRECTORY ${DIR})
        
        # 获取文件夹的名字（例如提取出 "pointers"）
        get_filename_component(DIR_NAME ${DIR} NAME)

        # 找到这个特定子文件夹下的所有 .cpp 文件 📄
        file(GLOB DIR_SOURCES "${DIR}/*.cpp")

        # 3. 只有当文件夹里确实有代码时，才生成对应的可执行程序 ⚙️
        if(DIR_SOURCES)
            # 生成的程序名将和文件夹同名！
            add_executable(${DIR_NAME} ${DIR_SOURCES})
        endif()
        
    endif()
endforeach()
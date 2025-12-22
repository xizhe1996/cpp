cmake_minimum_required(VERSION 3.1)

project(cpp_test VERSION 0.1)

# 禁用拷贝删除 强制编译器产生中间对象
add_compile_options(-fno-elide-constructors)

add_executable(test main.cpp)



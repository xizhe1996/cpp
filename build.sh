#!/bin/bash

# 遇到错误立即停止脚本
set -e

echo "build begin"

# 1. 检查并创建 build 文件夹
mkdir -p build

# 2. 进入 build 文件夹
cd build

# 3. 运行 CMake 生成 Makefile (指向上一级目录的 CMakeLists.txt)
cmake ..

# 4. 编译代码
make

echo "build end"

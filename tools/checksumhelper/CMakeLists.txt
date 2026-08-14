cmake_minimum_required(VERSION 3.16)
project(checksumhelper C)

# Must be built as 32-bit so it can load 32-bit DEVxxx.dll files
# Build with: cmake -G "MinGW Makefiles" -DCMAKE_C_FLAGS="-m32" ..
# Or use the build_win32.bat script

add_executable(checksumhelper main.c)

target_link_options(checksumhelper PRIVATE -m32 -static)
target_compile_options(checksumhelper PRIVATE -m32 -O2)

# No console window needed — we read stdout via QProcess
set_target_properties(checksumhelper PROPERTIES
    OUTPUT_NAME "checksumhelper"
)

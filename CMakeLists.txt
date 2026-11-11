cmake_minimum_required(VERSION 3.16)
project(chip8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(chip8 src/main.cpp src/chip8.cpp)
target_include_directories(chip8 PRIVATE src)

if(TARGET SDL2::SDL2)
    # SDL2main must come before SDL2: the linker resolves libraries in order.
    if(TARGET SDL2::SDL2main)
        target_link_libraries(chip8 PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(chip8 PRIVATE SDL2::SDL2)
else()
    target_include_directories(chip8 PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(chip8 PRIVATE ${SDL2_LIBRARIES})
endif()

if(MSVC)
    target_compile_options(chip8 PRIVATE /W4)
else()
    target_compile_options(chip8 PRIVATE -Wall -Wextra -O2)
endif()

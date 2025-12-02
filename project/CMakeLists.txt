cmake_minimum_required(VERSION 3.16)
project(BulletinBoard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE third_party)
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)


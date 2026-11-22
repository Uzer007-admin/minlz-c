cmake_minimum_required(VERSION 3.16)

project(minlz_package_consumer LANGUAGES C)

find_package(minlz 1.2.1 EXACT CONFIG REQUIRED)

add_executable(minlz_package_consumer main.c)
target_link_libraries(minlz_package_consumer PRIVATE minlz::minlz)

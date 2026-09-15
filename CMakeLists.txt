cmake_minimum_required(VERSION 3.22)

set(TARGET AutoSortLoot)
project(${TARGET})

add_library(${TARGET} SHARED
    "src/dllmain.cpp"
    "src/AutoSortHooks.cpp"
)

target_compile_options(AutoSortLoot PRIVATE /wd4996)

target_include_directories(${TARGET} PRIVATE .)

target_link_libraries(${TARGET} PRIVATE UE4SS)
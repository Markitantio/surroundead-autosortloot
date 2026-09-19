file(GLOB_RECURSE SMARTMEDIC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(SmartMedic SHARED ${SMARTMEDIC_SOURCES})

set_target_properties(SmartMedic PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

target_include_directories(SmartMedic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/UE4SS/include
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/UE4SS/generated_include
    ${CMAKE_SOURCE_DIR}/Output_ninja/RE-UE4SS/UE4SS/generated
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/Unreal/include
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/Unreal/generated_include
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/DynamicOutput/include
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/String/include
)

# ВАЖНО: замени link на то, что используется у AutoSortLoot.
# Скорее всего там линкуется UE4SS (или UE4SS-подобный таргет).
target_link_libraries(SmartMedic PRIVATE
    UE4SS
)
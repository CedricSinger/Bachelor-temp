cmake_minimum_required(VERSION 3.16)

find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)


find_package(nlohmann_json 3.9 QUIET)


include(FetchContent)

FetchContent_Declare(protozero
    GIT_REPOSITORY https://github.com/mapbox/protozero.git
    GIT_TAG        v1.7.1
)
FetchContent_GetProperties(protozero)
if(NOT protozero_POPULATED)
    FetchContent_Populate(protozero)
endif()

FetchContent_Declare(libosmium
    GIT_REPOSITORY https://github.com/osmcode/libosmium.git
    GIT_TAG        v2.20.0
)
FetchContent_GetProperties(libosmium)
if(NOT libosmium_POPULATED)
    FetchContent_Populate(libosmium)
endif()


add_executable(routenplaner
    src/main.cpp
    src/server.cpp
    src/graph.cpp
    src/router.cpp
    src/poi.cpp
)

target_include_directories(routenplaner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${protozero_SOURCE_DIR}/include
    ${libosmium_SOURCE_DIR}/include
)

target_link_libraries(routenplaner PRIVATE
    Boost::system
    Threads::Threads
    ZLIB::ZLIB
)

if(WIN32)
    target_link_libraries(routenplaner PRIVATE ws2_32 mswsock)
endif()

if(nlohmann_json_FOUND)
    target_link_libraries(routenplaner PRIVATE nlohmann_json::nlohmann_json)
else()
    message(STATUS "nlohmann_json not found via cmake, assuming header is in system include path")
endif()


add_custom_command(TARGET routenplaner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/frontend
    ${CMAKE_BINARY_DIR}/frontend
    COMMENT "Copying frontend files to build directory"
)

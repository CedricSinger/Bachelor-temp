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
    src/spatial_index.cpp
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
    target_compile_definitions(routenplaner PRIVATE _FILE_OFFSET_BITS=64)
endif()

if(nlohmann_json_FOUND)
    target_link_libraries(routenplaner PRIVATE nlohmann_json::nlohmann_json)
else()
    message(STATUS "nlohmann_json not found via cmake, assuming header is in system include path")
endif()


# Eigenstaendiges Analyse-Tool: Tag-Histogramm ueber alle OSM-Knoten.
add_executable(analyze_tags
    src/analyze_tags.cpp
)

target_include_directories(analyze_tags PRIVATE
    ${protozero_SOURCE_DIR}/include
    ${libosmium_SOURCE_DIR}/include
)

target_link_libraries(analyze_tags PRIVATE
    Threads::Threads
    ZLIB::ZLIB
)

if(WIN32)
    target_compile_definitions(analyze_tags PRIVATE _FILE_OFFSET_BITS=64)
endif()


# Mess-Tool: alle getaggten Knoten als POIs speichern + Abfragen messen.
add_executable(bench_pois
    src/bench_pois.cpp
    src/spatial_index.cpp
    src/poi.cpp
    src/graph.cpp
)

target_include_directories(bench_pois PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${protozero_SOURCE_DIR}/include
    ${libosmium_SOURCE_DIR}/include
)

target_link_libraries(bench_pois PRIVATE
    Threads::Threads
    ZLIB::ZLIB
)

if(WIN32)
    target_link_libraries(bench_pois PRIVATE psapi)
    target_compile_definitions(bench_pois PRIVATE _FILE_OFFSET_BITS=64)
endif()


add_executable(analyze_pois
    src/analyze_pois.cpp
)

target_include_directories(analyze_pois PRIVATE
    ${protozero_SOURCE_DIR}/include
    ${libosmium_SOURCE_DIR}/include
)

target_link_libraries(analyze_pois PRIVATE
    Threads::Threads
    ZLIB::ZLIB
)

if(WIN32)
    target_compile_definitions(analyze_pois PRIVATE _FILE_OFFSET_BITS=64)
endif()


add_custom_command(TARGET routenplaner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/frontend
    ${CMAKE_BINARY_DIR}/frontend
    COMMENT "Copying frontend files to build directory"
)

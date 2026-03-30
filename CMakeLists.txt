cmake_minimum_required(VERSION 3.21)

# Set vcpkg toolchain BEFORE project()
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")

project(main LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages installed by vcpkg
find_package(Crow CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

# SQLite amalgamation (compiled as C)
add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(main
    main.cpp
)

# Link Crow, Asio, and SQLite
target_link_libraries(main
    PRIVATE
        Crow::Crow
        asio::asio
        sqlite3
)

target_compile_features(main PRIVATE cxx_std_17)
target_compile_definitions(main PRIVATE _WIN32_WINNT=0x0A00)
cmake_minimum_required(VERSION 3.16)
project(ratehub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ratehub STATIC
    src/frame.cpp
    src/fleet.cpp
    src/shm_region.cpp
    src/ingest.cpp
    src/compute.cpp
    src/publish.cpp
    src/supervisor.cpp
    src/telemetry.cpp
    src/http_sse.cpp
    src/observe.cpp
)
target_include_directories(ratehub PUBLIC include)
target_compile_options(ratehub PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(make_replay tools/make_replay.cpp)
target_link_libraries(make_replay PRIVATE ratehub)
target_compile_options(make_replay PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ratehub_bench src/bench.cpp)
target_link_libraries(ratehub_bench PRIVATE ratehub)
target_compile_options(ratehub_bench PRIVATE -Wall -Wextra -Werror -Wpedantic -O2)

add_executable(ratehub_cli src/main.cpp)
set_target_properties(ratehub_cli PROPERTIES OUTPUT_NAME ratehub)
target_link_libraries(ratehub_cli PRIVATE ratehub)
target_compile_options(ratehub_cli PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ratehub_tests
    tests/main.cpp
    tests/test_frame.cpp
    tests/test_fleet.cpp
    tests/test_publish.cpp
    tests/test_system.cpp
    tests/test_pace.cpp
    tests/test_telemetry.cpp
    tests/test_http_sse.cpp
)
target_link_libraries(ratehub_tests PRIVATE ratehub)
target_compile_options(ratehub_tests PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_compile_definitions(ratehub_tests PRIVATE RATEHUB_BIN="$<TARGET_FILE:ratehub_cli>")
add_dependencies(ratehub_tests ratehub_cli)
find_package(Threads REQUIRED)
target_link_libraries(ratehub PUBLIC Threads::Threads)
if(NOT APPLE)
    target_link_libraries(ratehub PUBLIC rt)
endif()

add_custom_target(ratehub_web ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web
)
add_dependencies(ratehub_cli ratehub_web)

set(RATEHUB_SANITIZER "" CACHE STRING "address, undefined, or thread")
if(RATEHUB_SANITIZER STREQUAL "address")
    target_compile_options(ratehub PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ratehub PUBLIC -fsanitize=address,undefined)
elseif(RATEHUB_SANITIZER STREQUAL "thread")
    target_compile_options(ratehub PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(ratehub PUBLIC -fsanitize=thread)
endif()

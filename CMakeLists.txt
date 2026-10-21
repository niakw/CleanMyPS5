cmake_minimum_required(VERSION 3.16)
project(CleanMyPS5 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(ASSETS ${CMAKE_CURRENT_BINARY_DIR}/generated/assets.h)
add_custom_command(
 OUTPUT ${ASSETS}
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/embed_assets.py ${ASSETS}
         ${CMAKE_SOURCE_DIR}/web/index.html ${CMAKE_SOURCE_DIR}/web/styles.css ${CMAKE_SOURCE_DIR}/web/app.js
 DEPENDS ${CMAKE_SOURCE_DIR}/VERSION ${CMAKE_SOURCE_DIR}/scripts/embed_assets.py ${CMAKE_SOURCE_DIR}/web/index.html
         ${CMAKE_SOURCE_DIR}/web/styles.css ${CMAKE_SOURCE_DIR}/web/app.js
 VERBATIM
)
add_executable(cleanmyps5-server src/native/server.c src/native/inventory.c src/native/diagnostics.c src/native/duplicates.c src/native/media.c src/native/volumes.c src/native/video.c ${ASSETS})
target_include_directories(cleanmyps5-server PRIVATE src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cleanmyps5-server PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME native_server_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_server.py $<TARGET_FILE:cleanmyps5-server>)

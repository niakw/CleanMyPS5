cmake_minimum_required(VERSION 3.16)
project(CleanMyPS5 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)
set(ASSETS ${CMAKE_CURRENT_BINARY_DIR}/generated/assets.h)
set(RULES ${CMAKE_CURRENT_BINARY_DIR}/generated/scan_rules.h)
add_custom_command(OUTPUT ${RULES}
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/embed_rules.py
         ${CMAKE_SOURCE_DIR}/rules/metadata-v1.json ${RULES}
 DEPENDS ${CMAKE_SOURCE_DIR}/scripts/embed_rules.py ${CMAKE_SOURCE_DIR}/rules/metadata-v1.json
 VERBATIM)
add_custom_command(
 OUTPUT ${ASSETS}
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/embed_assets.py ${ASSETS}
         ${CMAKE_SOURCE_DIR}/web/index.html ${CMAKE_SOURCE_DIR}/web/styles.css ${CMAKE_SOURCE_DIR}/web/app.js
 DEPENDS ${CMAKE_SOURCE_DIR}/VERSION ${CMAKE_SOURCE_DIR}/scripts/embed_assets.py ${CMAKE_SOURCE_DIR}/web/index.html
         ${CMAKE_SOURCE_DIR}/web/styles.css ${CMAKE_SOURCE_DIR}/web/app.js
 VERBATIM
)
add_executable(cleanmyps5-server src/native/server.c src/native/inventory.c src/native/diagnostics.c src/native/duplicates.c src/native/media.c src/native/volumes.c src/native/video.c src/native/preflight.c src/native/homebrew.c ${ASSETS} ${RULES})
target_include_directories(cleanmyps5-server PRIVATE src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(cleanmyps5-server PRIVATE Threads::Threads)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cleanmyps5-server PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME native_server_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_server.py $<TARGET_FILE:cleanmyps5-server>)

add_test(NAME generic_inventory_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_inventory.py $<TARGET_FILE:cleanmyps5-server>)

add_executable(cmp5-duplicates-root-race tests/test_duplicates_root_race.c)
add_test(NAME duplicates_root_race COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_duplicates_root_race.py $<TARGET_FILE:cmp5-duplicates-root-race>)

# Exercise the same read-only metadata shortlist linked into the PS5 native UI.
add_executable(cmp5-native-review-tests tests/test_native_review.c src/native/inventory.c ${RULES})
target_include_directories(cmp5-native-review-tests PRIVATE src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_test(NAME native_review_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_native_review.py $<TARGET_FILE:cmp5-native-review-tests>)

# The same protected homebrew scanner is linked into the installed native title.
add_executable(cmp5-native-homebrew-tests tests/test_native_homebrew.c src/native/homebrew.c ${RULES})
target_include_directories(cmp5-native-homebrew-tests PRIVATE src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_test(NAME native_homebrew_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_native_homebrew.py $<TARGET_FILE:cmp5-native-homebrew-tests>)

# Safely read MP4 container metadata using a generated fake gallery.
# Does not require ffmpeg, a PS5 or user media.
add_test(NAME native_video_metadata_fixture
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_video_metadata.py)

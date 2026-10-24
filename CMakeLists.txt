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
# Reproduce EPERM on open, fstatat and openat in the real read-only
# helper preflight scanner without relying on filesystem ACLs on CI runners.
add_executable(cmp5-preflight-permissions
 tests/test_preflight_permission_errors.c ${RULES})
target_include_directories(cmp5-preflight-permissions PRIVATE
 src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-preflight-permissions PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
add_test(NAME preflight_permission_errors COMMAND cmp5-preflight-permissions)
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
add_executable(cmp5-homebrew-io-tests tests/test_homebrew_read_errors.c ${RULES})
target_include_directories(cmp5-homebrew-io-tests PRIVATE
 src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-homebrew-io-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME native_homebrew_fixture COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_native_homebrew.py
 $<TARGET_FILE:cmp5-native-homebrew-tests>
 $<TARGET_FILE:cmp5-homebrew-io-tests>)

# Deterministic identity races: replacement by rename or in-place
# metadata mutation between read and publication must fail closed.
add_executable(cmp5-native-autoload-identity-races
 tests/test_native_autoload_identity_races.c)
target_include_directories(cmp5-native-autoload-identity-races PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-native-autoload-identity-races PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME native_autoload_identity_races
 COMMAND cmp5-native-autoload-identity-races)

# Reuse the production autoload scanner in native title and helper. Verify
# aggregate counts without exposing config contents or enabling writes.
add_executable(cmp5-native-autoload-tests
 tests/test_native_autoload.c src/native/diagnostics.c)
target_include_directories(cmp5-native-autoload-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-native-autoload-tests PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME native_autoload_fixture COMMAND cmp5-native-autoload-tests)

# Fast native photo snapshot gate on every host and sanitizer runner;
# no full PS5 toolchain or image decoder required.
add_executable(cmp5-photo-snapshot-tests
 tests/test_photo_snapshot.c src/native/media_preview.c)
target_include_directories(cmp5-photo-snapshot-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-photo-snapshot-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME photo_snapshot_identity COMMAND
 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_photo_snapshot.py
 $<TARGET_FILE:cmp5-photo-snapshot-tests>)

# Ensure the real MP4/WebM snapshot reader and its malformed-format
# fixtures also execute on the faster host/macOS and sanitizer gates.
add_test(NAME detailed_video_snapshot_identity COMMAND
 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_video_metadata.py)

# Safely read MP4 container metadata using a generated fake gallery.
# Does not require ffmpeg, a PS5 or user media.
add_test(NAME native_video_metadata_fixture
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_video_metadata.py)

# Verified media backup: host-only until PS5 album-index consistency is proven.
# Never links into the installed title; no source deletion is implemented.
add_executable(cmp5-verified-backup-tests
 tests/test_verified_backup.c src/native/verified_backup.c src/native/sha256.c)
target_include_directories(cmp5-verified-backup-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-verified-backup-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME verified_backup_fixture
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_verified_backup.py
 $<TARGET_FILE:cmp5-verified-backup-tests>)

# The prototype MUST remain host-only until PS5 media-index restore is proven.
add_test(NAME verified_backup_activation_gate
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_backup_safety_gate.py)

# Atomic logical group of multiple sidecars; host-only until PS5 media database
# consistency, storage permissions, and power-loss recovery are verified.
add_executable(cmp5-verified-group-tests
 tests/test_verified_group.c src/native/verified_group.c
 src/native/capture_companions.c src/native/verified_backup.c src/native/sha256.c)
target_include_directories(cmp5-verified-group-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-verified-group-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME verified_group_fixture
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_verified_group.py
 $<TARGET_FILE:cmp5-verified-group-tests>)

# Metadata-only cross-tree gallery thumbnail observation with symlink and
# unknown-path fixtures; never modifies user media or reads image bodies.
add_executable(cmp5-gallery-thumbnail-tests
 tests/test_gallery_thumbnail.c src/native/capture_companions.c)
target_include_directories(cmp5-gallery-thumbnail-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-gallery-thumbnail-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(cmp5-gallery-thumbnail-denied
 tests/test_gallery_thumbnail_denied.c)
target_include_directories(cmp5-gallery-thumbnail-denied PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-gallery-thumbnail-denied PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME gallery_thumbnail_contract
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gallery_thumbnail.py
  $<TARGET_FILE:cmp5-gallery-thumbnail-tests>
  $<TARGET_FILE:cmp5-gallery-thumbnail-denied>)

# Host-only four-member path: an externally located Gallery thumbnail
# must be copied and SHA-256 verified in a separate private workspace.
# This does not restore any PS5 Gallery media or modify Sony indexes.
add_executable(cmp5-thumbnail-private-backup-tests
 tests/test_gallery_thumbnail_backup.c src/native/capture_companions.c
 src/native/verified_group.c src/native/verified_backup.c src/native/sha256.c)
target_include_directories(cmp5-thumbnail-private-backup-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-thumbnail-private-backup-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME gallery_thumbnail_private_backup
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gallery_thumbnail_backup.py
 $<TARGET_FILE:cmp5-thumbnail-private-backup-tests>)

# Deterministic metadata race while the Gallery sidecar is being inspected.
add_executable(cmp5-gallery-sidecar-race tests/test_gallery_sidecar_race.c ${RULES})
target_include_directories(cmp5-gallery-sidecar-race PRIVATE src/native ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_test(NAME gallery_sidecar_race
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gallery_sidecar_race.py
 $<TARGET_FILE:cmp5-gallery-sidecar-race>)

# Host-only private backup capsule identity tests; no PS5 media writes.
add_executable(cmp5-backup-identity-races
 tests/test_backup_identity_races.c src/native/sha256.c)
target_include_directories(cmp5-backup-identity-races PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-backup-identity-races PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME backup_identity_races COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_backup_identity_races.py
 $<TARGET_FILE:cmp5-backup-identity-races>)

# Host-only race harness: a valid SHA-256 is not enough if the private
# manifest or restored filename changed while being read.
add_executable(cmp5-group-identity-races
 tests/test_group_identity_races.c
 src/native/capture_companions.c src/native/verified_backup.c src/native/sha256.c)
target_include_directories(cmp5-group-identity-races PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-group-identity-races PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME group_identity_races COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_group_identity_races.py
 $<TARGET_FILE:cmp5-group-identity-races>)

# Bounded game image discovery: filename and stat only, NEVER game contents,
# never rewriting disk images or claiming native LZ4 launch compatibility.
add_executable(cmp5-game-images-tests
 tests/test_game_images.c src/native/game_images.c)
target_include_directories(cmp5-game-images-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-game-images-tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME game_images_fixture COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_game_images.py
 $<TARGET_FILE:cmp5-game-images-tests>)

# Exercise actual game scanner failures on host/macOS/sanitizers.
add_executable(cmp5-game-images-read-errors tests/test_game_images_read_errors.c)
target_include_directories(cmp5-game-images-read-errors PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-game-images-read-errors PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME game_images_read_errors COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_game_images_read_errors.py
 $<TARGET_FILE:cmp5-game-images-read-errors>)

# Host-only shared capacity gate. Simulate full/read-only/unknown volumes
# before starting any private backup or private restore transaction.
add_executable(cmp5-private-workspace-capacity
 tests/test_private_workspace_capacity.c)
target_include_directories(cmp5-private-workspace-capacity PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-private-workspace-capacity PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME private_workspace_capacity COMMAND cmp5-private-workspace-capacity)

# Concurrent, multi-member private backup and restore proof. Serializes
# all transaction IDs in a private workspace, without console writes.
add_executable(cmp5-group-lock-tests
 tests/test_group_transaction_lock.c src/native/verified_group.c
 src/native/capture_companions.c src/native/verified_backup.c
 src/native/sha256.c)
target_include_directories(cmp5-group-lock-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-group-lock-tests PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME group_transaction_lock COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_group_transaction_lock.py
 $<TARGET_FILE:cmp5-group-lock-tests>)

# Strictly isolated host-only lifecycle proof: verified capsule, independent
# restore rehearsal, quarantined private item and exact private recovery.
# NEVER link the implementation into the PS5 title or helper.
add_executable(cmp5-private-quarantine-tests
 tests/test_private_quarantine.c src/native/private_quarantine.c
 src/native/verified_backup.c src/native/sha256.c)
target_include_directories(cmp5-private-quarantine-tests PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-private-quarantine-tests PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME private_quarantine_lifecycle COMMAND ${Python3_EXECUTABLE}
 ${CMAKE_SOURCE_DIR}/tests/test_private_quarantine.py
 $<TARGET_FILE:cmp5-private-quarantine-tests>)

# Host-only private transaction workspaces: reject privileged PS5 roots,
# parent symlink swaps, path traversal and directories not owned as 0700.
add_executable(cmp5-private-workspace-guard tests/test_private_workspace_guard.c)
target_include_directories(cmp5-private-workspace-guard PRIVATE src/native)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(cmp5-private-workspace-guard PRIVATE
   -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME private_workspace_guard COMMAND cmp5-private-workspace-guard)

# Optional CMake glue for the host-side verification.
#
# The canonical runner is run_host_tests.sh, which needs only a compiler (plus
# Node for the UI contract test). This file exists so the same checks can join
# the project's normal CTest gate:
#
#     # top-level CMakeLists.txt
#     add_subdirectory(test/host)
#
# Every target is created only when its inputs exist, so this directory is
# harmless on a branch that does not carry the firmware yet.
#
# For the firmware harness, point WALLCLOCK_ARDUINOJSON_DIR at an ArduinoJson
# source tree (the directory containing ArduinoJson.h), e.g.
#
#     cmake -S . -B build \
#       -DWALLCLOCK_ARDUINOJSON_DIR=$PWD/.deps/ArduinoJson/src

get_filename_component(WALLCLOCK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(WALLCLOCK_HOST_FLAGS -Wall -Wextra)
set(WALLCLOCK_SANITIZER_FLAGS "")
option(WALLCLOCK_SANITIZERS "Build host tests with ASan/UBSan" OFF)
if(WALLCLOCK_SANITIZERS)
  set(WALLCLOCK_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# --- engine property suite -------------------------------------------------
if(EXISTS "${WALLCLOCK_ROOT}/include/clock_engine.h")
  add_executable(engine_properties_test engine_properties.cpp)
  target_include_directories(engine_properties_test PRIVATE "${WALLCLOCK_ROOT}/include")
  target_compile_options(engine_properties_test PRIVATE ${WALLCLOCK_HOST_FLAGS} ${WALLCLOCK_SANITIZER_FLAGS})
  target_link_options(engine_properties_test PRIVATE ${WALLCLOCK_SANITIZER_FLAGS})
  add_test(NAME engine_properties COMMAND engine_properties_test)
endif()

# --- firmware integration harness -----------------------------------------
# The explicit gate must not silently omit firmware integration coverage.
option(WALLCLOCK_REQUIRE_FIRMWARE_TESTS "Fail configuration if the firmware harness cannot be built" OFF)
if(WALLCLOCK_REQUIRE_FIRMWARE_TESTS)
  if(NOT EXISTS "${WALLCLOCK_ROOT}/src/main.cpp")
    message(FATAL_ERROR "WALLCLOCK_REQUIRE_FIRMWARE_TESTS requires src/main.cpp")
  endif()
  if(NOT WALLCLOCK_ARDUINOJSON_DIR OR NOT EXISTS "${WALLCLOCK_ARDUINOJSON_DIR}/ArduinoJson.h")
    message(FATAL_ERROR "WALLCLOCK_REQUIRE_FIRMWARE_TESTS requires WALLCLOCK_ARDUINOJSON_DIR containing ArduinoJson.h")
  endif()
endif()

if(EXISTS "${WALLCLOCK_ROOT}/src/main.cpp" AND WALLCLOCK_ARDUINOJSON_DIR)
  add_executable(firmware_host_harness firmware_harness.cpp stubs/host_stubs.cpp)
  target_compile_definitions(firmware_host_harness PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=1)
  target_include_directories(firmware_host_harness PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${WALLCLOCK_ROOT}/include")
  target_include_directories(firmware_host_harness SYSTEM PRIVATE "${WALLCLOCK_ARDUINOJSON_DIR}")
  set_target_properties(firmware_host_harness PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS ON)
  target_compile_options(firmware_host_harness PRIVATE ${WALLCLOCK_HOST_FLAGS} -O0 ${WALLCLOCK_SANITIZER_FLAGS})
  target_link_options(firmware_host_harness PRIVATE ${WALLCLOCK_SANITIZER_FLAGS})
  add_test(NAME firmware_host COMMAND firmware_host_harness)
elseif(EXISTS "${WALLCLOCK_ROOT}/src/main.cpp")
  message(STATUS "firmware_host_harness skipped: set WALLCLOCK_ARDUINOJSON_DIR to build it")
endif()

# --- local serial configuration tool ---------------------------------------
find_program(WALLCLOCK_PYTHON NAMES python3)
if(UNIX AND WALLCLOCK_PYTHON AND EXISTS "${WALLCLOCK_ROOT}/test/security/serial_config_test.py")
  add_test(NAME serial_config_tool
           COMMAND "${WALLCLOCK_PYTHON}" "${WALLCLOCK_ROOT}/test/security/serial_config_test.py")
elseif(WALLCLOCK_REQUIRE_FIRMWARE_TESTS)
  message(FATAL_ERROR "Serial configuration tests require Python 3 and POSIX pseudo-terminals")
endif()

# --- browser UI contract ---------------------------------------------------
find_program(WALLCLOCK_NODE NAMES node nodejs)
if(WALLCLOCK_NODE AND EXISTS "${WALLCLOCK_ROOT}/include/web_ui.h")
  add_test(NAME ui_contract
           COMMAND "${WALLCLOCK_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/ui_contract_test.mjs" --root "${WALLCLOCK_ROOT}")
endif()

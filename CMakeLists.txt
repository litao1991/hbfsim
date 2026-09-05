cmake_minimum_required(VERSION 3.20)
project(hbfsim VERSION 0.6.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hbfsim_core
  src/hbf_system.cpp
  src/protocol/protocol.cpp
  src/protocol/frontend.cpp
  src/controller/base_die.cpp
  src/controller/scheduler.cpp
  src/controller/interconnect.cpp
  src/config/config.cpp
  src/config/resolved_config.cpp
  src/frontend/csv_trace.cpp
  src/kernel/event_queue.cpp
  src/kernel/events.cpp
  src/kernel/simulator.cpp
  src/media/media.cpp
  src/media/reliability.cpp
  src/mapping/mapper.cpp
  src/management/refresh.cpp
  src/extensions/stripe_mapping.cpp
  src/extensions/copy_engine.cpp
  src/extensions/host_replay.cpp
  src/extensions/copy_engine_state.cpp
  src/extensions/copy_gc.cpp
  src/stats/resource_tracker.cpp
  src/stats/stats.cpp
)
target_include_directories(hbfsim_core PUBLIC include)
target_compile_options(hbfsim_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hbfsim src/main.cpp)
target_link_libraries(hbfsim PRIVATE hbfsim_core)

include(CTest)
if(BUILD_TESTING)
  foreach(test_name mapper stripe_mapping parallelism_group copy_engine host_gc refresh_manager wear model_validation scheduler timing erase resources channels warmup advanced v011 component_boundaries batch_read host_retry host_replay zone_placement host_management)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_link_libraries(test_${test_name} PRIVATE hbfsim_core)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
  foreach(spec_test_name profile response system_boundary channel axi status dlu semantic media_mapping read_cache)
    add_executable(test_spec_${spec_test_name}
      tests/spec/test_${spec_test_name}.cpp)
    target_link_libraries(test_spec_${spec_test_name} PRIVATE hbfsim_core)
    add_test(NAME spec_${spec_test_name} COMMAND test_spec_${spec_test_name})
    set_tests_properties(spec_${spec_test_name}
      PROPERTIES LABELS "spec;compliance")
  endforeach()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME experiment_runner
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_SOURCE_DIR}/tests/test_experiment_runner.py
              ${CMAKE_SOURCE_DIR}/tools/experiment_runner.py)
  endif()
endif()

# Each target is a libFuzzer entry point. With MOD_BUILD_FUZZERS (clang) it becomes a
# fuzzer, fuzz_<name>; with MOD_BUILD_TESTS it is also linked with replay_main into
# fuzz_replay_<name>, which ctest runs over corpus/<name>, so every input a fuzzer ever
# needed, crashes included, stays in the suite of every build.
set(MOD_FUZZ_TARGETS json config color_spec cli input utf8 lsp_frames regex markdown editor sidecar)

foreach(name IN LISTS MOD_FUZZ_TARGETS)
  if(MOD_BUILD_FUZZERS)
    add_executable(fuzz_${name} fuzz_${name}.cpp)
    target_link_libraries(fuzz_${name} PRIVATE mod_core)
    target_include_directories(fuzz_${name} PRIVATE ${PROJECT_SOURCE_DIR})
    mod_target_options(fuzz_${name})
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
  endif()
  if(MOD_BUILD_TESTS)
    add_executable(fuzz_replay_${name} fuzz_${name}.cpp replay_main.cpp)
    target_link_libraries(fuzz_replay_${name} PRIVATE mod_core)
    target_include_directories(fuzz_replay_${name} PRIVATE ${PROJECT_SOURCE_DIR})
    mod_target_options(fuzz_replay_${name})
    add_test(NAME fuzz_replay_${name} COMMAND fuzz_replay_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
  endif()
endforeach()

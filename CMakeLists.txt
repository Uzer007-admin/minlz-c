cmake_minimum_required(VERSION 3.16)

project(minlz_c VERSION 1.2.5 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads)

option(MINLZ_BUILD_CLI "Build the mz command line tool" ON)
option(MINLZ_BUILD_TESTS "Build C tests" ON)
option(MINLZ_BUILD_BENCH "Build benchmark helpers" ON)
option(MINLZ_BUILD_PERF "Build performance comparison runners" ON)
option(MINLZ_ENABLE_INTRINSICS "Enable runtime-dispatched compiler intrinsic fast paths" ON)
option(MINLZ_ENABLE_INLINE_ASM "Enable inline assembly hot paths inside C sources" ON)
option(MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH "Enable the unaccepted exact-output L2 folded-search asm candidate" OFF)
option(MINLZ_ENABLE_HUGEPAGE_WORKSPACE "Hint Linux THP for reused large encoder scratch buffers" OFF)
option(MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER "Transfer validated workspace buffers into the indexed reader cache" OFF)
option(MINLZ_ENABLE_LTO "Enable Release interprocedural optimization when supported" ON)
option(MINLZ_FULL_GO_PARITY "Require optional pieces for complete Go CLI/API parity" OFF)
set(MINLZ_ENABLE_ASM "AUTO" CACHE STRING "Enable optional assembly fast paths: AUTO, ON, or OFF")
option(MINLZ_ENABLE_NATIVE "Build with compiler native tuning flags" OFF)
set(MINLZ_WITH_FALLBACK "AUTO" CACHE STRING "Snappy/S2 fallback mode: AUTO, ON, or OFF")
set(MINLZ_GO_REPO "" CACHE PATH "Optional path to the Go minlz repository for compatibility CTest checks")
set_property(CACHE MINLZ_WITH_FALLBACK PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE MINLZ_ENABLE_ASM PROPERTY STRINGS AUTO ON OFF)

set(MINLZ_IPO_SUPPORTED OFF)
if(MINLZ_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT MINLZ_IPO_SUPPORTED OUTPUT MINLZ_IPO_OUTPUT LANGUAGES C)
    if(NOT MINLZ_IPO_SUPPORTED)
        message(STATUS "Release IPO/LTO disabled: ${MINLZ_IPO_OUTPUT}")
    endif()
endif()

function(minlz_enable_release_ipo target_name)
    if(MINLZ_ENABLE_LTO AND MINLZ_IPO_SUPPORTED)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

set(MINLZ_SOURCES
    src/block_decode.c
    src/block_decode_goasm.c
    src/block_encode.c
    src/block_encode_better_goasm1k.c
    src/block_encode_better_goasm4k.c
    src/block_encode_better_goasm16k.c
    src/block_encode_better_goasm64k.c
    src/block_encode_better_goasm512k.c
    src/block_encode_better_goasm8m.c
    src/block_encode_fast_goasm2mb.c
    src/block_encode_l1_goasm2mb.c
    src/block_encode_l1_goasm8m.c
    src/block_encode_goasm2mb.c
    src/cpu.c
    src/crc32c.c
    src/emit.c
    src/index.c
    src/io_file.c
    src/search.c
    src/search_huff0.c
    src/stream.c
    src/third_party/zstd/common/entropy_common.c
    src/third_party/zstd/common/error_private.c
    src/third_party/zstd/common/fse_decompress.c
    src/third_party/zstd/compress/fse_compress.c
    src/third_party/zstd/compress/hist.c
    src/third_party/zstd/compress/huf_compress.c
    src/third_party/zstd/decompress/huf_decompress.c
    src/util.c
    src/workspace.c
)

set(MINLZ_THIRD_PARTY_ZSTD_SOURCES
    src/third_party/zstd/common/entropy_common.c
    src/third_party/zstd/common/error_private.c
    src/third_party/zstd/common/fse_decompress.c
    src/third_party/zstd/compress/fse_compress.c
    src/third_party/zstd/compress/hist.c
    src/third_party/zstd/compress/huf_compress.c
    src/third_party/zstd/decompress/huf_decompress.c
)

set(MINLZ_GLOBAL_ASM_C_SOURCES
    src/block_decode_goasm.c
    src/block_encode_better_goasm1k.c
    src/block_encode_better_goasm4k.c
    src/block_encode_better_goasm16k.c
    src/block_encode_better_goasm64k.c
    src/block_encode_better_goasm512k.c
    src/block_encode_better_goasm8m.c
    src/block_encode_fast_goasm2mb.c
    src/block_encode_l1_goasm2mb.c
    src/block_encode_l1_goasm8m.c
    src/block_encode_goasm2mb.c
)

if(NOT MSVC)
    set_source_files_properties(${MINLZ_GLOBAL_ASM_C_SOURCES} PROPERTIES COMPILE_OPTIONS "-fno-lto")
endif()

set(MINLZ_X86 0)
set(MINLZ_X86_64 0)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    set(MINLZ_X86 1)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(MINLZ_X86_64 1)
endif()

set(MINLZ_HAVE_AVX2 0)
set(MINLZ_HAVE_AVX512 0)
set(MINLZ_ZSTD_DYNAMIC_BMI2 0)
if(MINLZ_ENABLE_INTRINSICS AND MINLZ_X86 AND NOT MSVC)
    list(APPEND MINLZ_SOURCES src/block_decode_avx2.c src/block_decode_avx512.c)
    set_source_files_properties(src/block_decode_avx2.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mbmi;-mbmi2")
    set_source_files_properties(src/block_decode_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512bw;-mavx512vl")
    set(MINLZ_HAVE_AVX2 1)
    set(MINLZ_HAVE_AVX512 1)
    set(MINLZ_ZSTD_DYNAMIC_BMI2 1)
endif()

set(MINLZ_HAVE_ASM_AMD64 0)
if(NOT MINLZ_ENABLE_ASM STREQUAL "OFF" AND MINLZ_X86_64 AND NOT MSVC)
    enable_language(ASM)
    list(APPEND MINLZ_SOURCES src/copy_amd64.S)
    set(MINLZ_HAVE_ASM_AMD64 1)
endif()

add_library(minlz ${MINLZ_SOURCES})
add_library(minlz::minlz ALIAS minlz)
minlz_enable_release_ipo(minlz)
target_include_directories(minlz
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/zstd/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/zstd/compress
)
target_compile_features(minlz PUBLIC c_std_11)
if(MSVC)
    target_compile_options(minlz PRIVATE /W4)
    set_source_files_properties(${MINLZ_THIRD_PARTY_ZSTD_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/w")
else()
    target_compile_options(minlz PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(${MINLZ_THIRD_PARTY_ZSTD_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w")
endif()
target_compile_definitions(minlz PRIVATE
    MINLZ_ENABLE_INTRINSICS=$<BOOL:${MINLZ_ENABLE_INTRINSICS}>
    MINLZ_ENABLE_INLINE_ASM=$<BOOL:${MINLZ_ENABLE_INLINE_ASM}>
    MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH=$<BOOL:${MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH}>
    MINLZ_ENABLE_HUGEPAGE_WORKSPACE=$<BOOL:${MINLZ_ENABLE_HUGEPAGE_WORKSPACE}>
    MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER=$<BOOL:${MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER}>
    MINLZ_HAVE_AVX2=${MINLZ_HAVE_AVX2}
    MINLZ_HAVE_AVX512=${MINLZ_HAVE_AVX512}
    MINLZ_HAVE_ASM_AMD64=${MINLZ_HAVE_ASM_AMD64}
    MINLZ_WITH_FALLBACK=0
    # The vendored entropy subset intentionally excludes zstd's external
    # assembly loops. Keep its C/BMI2 dispatcher self-contained in every
    # build type, including Debug and sanitizer builds.
    ZSTD_DISABLE_ASM=1
    DYNAMIC_BMI2=${MINLZ_ZSTD_DYNAMIC_BMI2}
)
if(Threads_FOUND)
    target_compile_definitions(minlz PRIVATE MINLZ_HAVE_THREADS=1)
    target_link_libraries(minlz PRIVATE Threads::Threads)
else()
    target_compile_definitions(minlz PRIVATE MINLZ_HAVE_THREADS=0)
endif()
if(MINLZ_ENABLE_NATIVE AND NOT MSVC)
    target_compile_options(minlz PRIVATE -march=native)
endif()

if(NOT MINLZ_WITH_FALLBACK STREQUAL "OFF")
    message(STATUS "Snappy/S2 fallback is optional; no bundled fallback dependency is required for this build")
endif()
if(MINLZ_FULL_GO_PARITY)
    message(FATAL_ERROR "MINLZ_FULL_GO_PARITY requires Snappy/S2 fallback, HTTP/TLS CLI support, and LZ4 converter backends that are not bundled in this C port yet")
endif()

if(MINLZ_BUILD_CLI)
    add_executable(mz tools/mz.c tools/mz_stats.c)
    minlz_enable_release_ipo(mz)
    target_link_libraries(mz PRIVATE minlz)
    if(NOT MSVC)
        target_compile_options(mz PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MINLZ_BUILD_PERF)
    add_executable(minlz_perf tools/minlz_perf.c)
    target_compile_definitions(minlz_perf PRIVATE
        MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH=$<BOOL:${MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH}>
        MINLZ_ENABLE_HUGEPAGE_WORKSPACE=$<BOOL:${MINLZ_ENABLE_HUGEPAGE_WORKSPACE}>)
    minlz_enable_release_ipo(minlz_perf)
    target_link_libraries(minlz_perf PRIVATE minlz)
    target_include_directories(minlz_perf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_executable(minlz_thread_perf tools/minlz_thread_perf.c)
    minlz_enable_release_ipo(minlz_thread_perf)
    target_link_libraries(minlz_thread_perf PRIVATE minlz)
    if(NOT MSVC)
        target_compile_options(minlz_perf PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(minlz_thread_perf PRIVATE -Wall -Wextra -Wpedantic)
        if(MINLZ_ENABLE_NATIVE)
            target_compile_options(minlz_perf PRIVATE -march=native)
            target_compile_options(minlz_thread_perf PRIVATE -march=native)
        endif()
    endif()
endif()

if(MINLZ_BUILD_BENCH)
    add_executable(minlz_bench tools/minlz_bench.c)
    minlz_enable_release_ipo(minlz_bench)
    target_link_libraries(minlz_bench PRIVATE minlz)
    add_executable(minlz_compare_old tools/minlz_compare_old.c)
    minlz_enable_release_ipo(minlz_compare_old)
    target_link_libraries(minlz_compare_old PRIVATE minlz)
    if(NOT MSVC)
        target_compile_options(minlz_bench PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(minlz_compare_old PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MINLZ_BUILD_TESTS)
    enable_testing()
    add_executable(minlz_workspace_unit tests/test_workspace.c src/workspace.c)
    target_include_directories(minlz_workspace_unit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(minlz_workspace_unit PRIVATE
        MINLZ_ENABLE_HUGEPAGE_WORKSPACE=$<BOOL:${MINLZ_ENABLE_HUGEPAGE_WORKSPACE}>
        MINLZ_ENABLE_INTRINSICS=0 MINLZ_ENABLE_INLINE_ASM=0
        MINLZ_HAVE_AVX2=0 MINLZ_HAVE_AVX512=0 MINLZ_HAVE_ASM_AMD64=0)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(minlz_workspace_unit PRIVATE MINLZ_TEST_WRAP_MADVISE=1)
        target_link_options(minlz_workspace_unit PRIVATE "-Wl,--wrap=madvise")
    endif()
    add_test(NAME minlz_workspace_unit COMMAND minlz_workspace_unit)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME minlz_report_cpu_gate
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_minlz_report.py)
    endif()
    add_executable(minlz_unit tests/test_minlz.c)
    target_compile_definitions(minlz_unit PRIVATE
        MINLZ_ENABLE_HUGEPAGE_WORKSPACE=$<BOOL:${MINLZ_ENABLE_HUGEPAGE_WORKSPACE}>)
    minlz_enable_release_ipo(minlz_unit)
    target_link_libraries(minlz_unit PRIVATE minlz)
    target_include_directories(minlz_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME minlz_unit COMMAND minlz_unit)
    add_test(NAME minlz_unit_portable_cpu COMMAND minlz_unit)
    set_tests_properties(minlz_unit_portable_cpu PROPERTIES ENVIRONMENT "MINLZ_FORCE_CPU=portable")
    add_test(NAME minlz_unit_sse42_cpu COMMAND minlz_unit)
    set_tests_properties(minlz_unit_sse42_cpu PROPERTIES ENVIRONMENT "MINLZ_FORCE_CPU=sse42")
    add_test(NAME minlz_unit_avx2_cpu COMMAND minlz_unit)
    set_tests_properties(minlz_unit_avx2_cpu PROPERTIES ENVIRONMENT "MINLZ_FORCE_CPU=avx2")
    add_test(NAME minlz_unit_asm_cpu COMMAND minlz_unit)
    set_tests_properties(minlz_unit_asm_cpu PROPERTIES ENVIRONMENT "MINLZ_FORCE_CPU=asm")
    add_test(NAME minlz_unit_avx512_probe COMMAND minlz_unit)
    set_tests_properties(minlz_unit_avx512_probe PROPERTIES ENVIRONMENT "MINLZ_FORCE_CPU=avx512")
    set_tests_properties(
        minlz_unit minlz_unit_portable_cpu minlz_unit_sse42_cpu
        minlz_unit_avx2_cpu minlz_unit_asm_cpu minlz_unit_avx512_probe
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    if(MINLZ_BUILD_CLI AND MINLZ_GO_REPO)
        find_program(GO_EXECUTABLE go)
        set(GO_COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/compat/minlz-go-compat/main.go)
        if(GO_EXECUTABLE AND EXISTS ${GO_COMPAT} AND EXISTS ${MINLZ_GO_REPO}/go.mod)
            file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test-out)
            set(TEST_TXT ${CMAKE_CURRENT_SOURCE_DIR}/testdata/Mark.Twain-Tom.Sawyer.txt)
            set(TEST_OUT ${CMAKE_CURRENT_BINARY_DIR}/test-out)

            # Use the module in the selected checkout, including public forks.
            file(STRINGS "${MINLZ_GO_REPO}/go.mod" GO_MODULE_LINE REGEX "^module[ \t]+")
            string(REGEX REPLACE "^module[ \t]+([^ \t]+).*" "\\1" GO_MODULE "${GO_MODULE_LINE}")
            if(NOT GO_MODULE OR GO_MODULE STREQUAL GO_MODULE_LINE)
                message(FATAL_ERROR "Cannot read module path from ${MINLZ_GO_REPO}/go.mod")
            endif()
            file(READ "${GO_COMPAT}" GO_COMPAT_CONTENT)
            string(REPLACE "github.com/minio/minlz" "${GO_MODULE}" GO_COMPAT_CONTENT "${GO_COMPAT_CONTENT}")
            file(MAKE_DIRECTORY "${TEST_OUT}/compat")
            set(GO_COMPAT "${TEST_OUT}/compat/main.go")
            file(WRITE "${GO_COMPAT}" "${GO_COMPAT_CONTENT}")

            if(WIN32 AND MINLZ_BUILD_PERF)
                set(GO_PERF_ORACLE "${TEST_OUT}/go perf oracle.exe")
                add_test(NAME build_go_perf_oracle
                    COMMAND ${GO_EXECUTABLE} build -o "${GO_PERF_ORACLE}" ${GO_COMPAT})
                set_tests_properties(build_go_perf_oracle PROPERTIES
                    WORKING_DIRECTORY ${MINLZ_GO_REPO}
                    FIXTURES_SETUP go_perf_oracle)
                add_test(NAME minlz_perf_go_oracle_quoted_path
                    COMMAND ${CMAKE_COMMAND} -E env
                        "MINLZ_GO_COMPAT_BIN=${GO_PERF_ORACLE}"
                        "MINLZ_PERF_TMPDIR=${TEST_OUT}"
                        $<TARGET_FILE:minlz_perf> ${TEST_TXT}
                        --iterations 1 --samples 1 --corpus small-16 --level 2
                        --ops block --go-oracle --strict-go-size)
                set_tests_properties(minlz_perf_go_oracle_quoted_path PROPERTIES
                    FIXTURES_REQUIRED go_perf_oracle)
            endif()

            add_test(NAME go_encode_block
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} encode-block ${TEST_TXT} ${TEST_OUT}/go.mzb)
            set_tests_properties(go_encode_block PROPERTIES WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_decode_go_block COMMAND mz d -block -q -o ${TEST_OUT}/go.dec ${TEST_OUT}/go.mzb)
            set_tests_properties(c_decode_go_block PROPERTIES DEPENDS go_encode_block)
            add_test(NAME compare_go_block COMMAND ${CMAKE_COMMAND} -E compare_files ${TEST_TXT} ${TEST_OUT}/go.dec)
            set_tests_properties(compare_go_block PROPERTIES DEPENDS c_decode_go_block)

            add_test(NAME c_encode_block COMMAND mz c -block -q -o ${TEST_OUT}/c.mzb ${TEST_TXT})
            add_test(NAME go_decode_c_block
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} decode-block ${TEST_OUT}/c.mzb ${TEST_OUT}/c.dec)
            set_tests_properties(go_decode_c_block PROPERTIES DEPENDS c_encode_block WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_block COMMAND ${CMAKE_COMMAND} -E compare_files ${TEST_TXT} ${TEST_OUT}/c.dec)
            set_tests_properties(compare_c_block PROPERTIES DEPENDS go_decode_c_block)

            add_test(NAME go_encode_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} encode-stream ${TEST_TXT} ${TEST_OUT}/go.mz)
            set_tests_properties(go_encode_stream PROPERTIES WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_decode_go_stream COMMAND mz d -q -o ${TEST_OUT}/go-stream.dec ${TEST_OUT}/go.mz)
            set_tests_properties(c_decode_go_stream PROPERTIES DEPENDS go_encode_stream)
            add_test(NAME compare_go_stream COMMAND ${CMAKE_COMMAND} -E compare_files ${TEST_TXT} ${TEST_OUT}/go-stream.dec)
            set_tests_properties(compare_go_stream PROPERTIES DEPENDS c_decode_go_stream)

            add_test(NAME c_encode_stream COMMAND mz c -q -o ${TEST_OUT}/c.mz ${TEST_TXT})
            add_test(NAME go_decode_c_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} decode-stream ${TEST_OUT}/c.mz ${TEST_OUT}/c-stream.dec)
            set_tests_properties(go_decode_c_stream PROPERTIES DEPENDS c_encode_stream WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_stream COMMAND ${CMAKE_COMMAND} -E compare_files ${TEST_TXT} ${TEST_OUT}/c-stream.dec)
            set_tests_properties(compare_c_stream PROPERTIES DEPENDS go_decode_c_stream)

            function(minlz_add_level_compat_tests level_name level_value level_flag)
                foreach(format IN ITEMS block stream)
                    foreach(direction IN ITEMS go-to-c c-to-go)
                        set(case_name compat-${direction}-${format}-${level_name})
                        add_test(NAME ${case_name}
                            COMMAND ${CMAKE_COMMAND}
                                -DDIRECTION=${direction}
                                -DFORMAT=${format}
                                -DLEVEL=${level_value}
                                -DLEVEL_FLAG=${level_flag}
                                -DGO=${GO_EXECUTABLE}
                                -DGO_REPO=${MINLZ_GO_REPO}
                                -DGO_COMPAT=${GO_COMPAT}
                                -DMZ=$<TARGET_FILE:mz>
                                -DINPUT=${TEST_TXT}
                                -DOUT_DIR=${TEST_OUT}/levels
                                -DCASE_NAME=${case_name}
                                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat_levels.cmake)
                    endforeach()
                endforeach()
            endfunction()

            minlz_add_level_compat_tests(xfast -1 -xfast)
            minlz_add_level_compat_tests(uncompressed 0 -0)
            minlz_add_level_compat_tests(fastest 1 -1)
            minlz_add_level_compat_tests(balanced 2 -2)
            minlz_add_level_compat_tests(smallest 3 -3)

            set(SEARCH_CAPTURE ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_search.cmake)
            set(GO_SEARCH_CAPTURE ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_go_search.cmake)
            set(SEARCH_COMPARE ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_search.cmake)
            set(CONCAT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tests/concat_files.cmake)
            set(PREFIX_INPUT ${TEST_OUT}/prefix-only.txt)
            set(PREFIX_EXPECTED ${TEST_OUT}/prefix-only.expected)

            add_test(NAME make_prefix_only_input
                COMMAND ${CMAKE_COMMAND}
                        -DOUTPUT=${PREFIX_INPUT}
                        -DEXPECTED=${PREFIX_EXPECTED}
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_prefix_only_input.cmake)

            add_test(NAME c_encode_prefix_only_search
                COMMAND mz c -q -bs 4096
                        -search -search.prefix co64 -search.uncompressed
                        -o ${TEST_OUT}/c-prefix-only.mz ${PREFIX_INPUT})
            set_tests_properties(c_encode_prefix_only_search PROPERTIES
                DEPENDS make_prefix_only_input)
            add_test(NAME go_decode_c_prefix_only
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} decode-stream
                        ${TEST_OUT}/c-prefix-only.mz
                        ${TEST_OUT}/go-decode-c-prefix-only.txt)
            set_tests_properties(go_decode_c_prefix_only PROPERTIES
                DEPENDS c_encode_prefix_only_search
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_prefix_only_stream
                COMMAND ${CMAKE_COMMAND} -E compare_files
                        ${PREFIX_INPUT}
                        ${TEST_OUT}/go-decode-c-prefix-only.txt)
            set_tests_properties(compare_c_prefix_only_stream PROPERTIES
                DEPENDS go_decode_c_prefix_only)
            add_test(NAME c_search_c_prefix_only
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/c-prefix-only.mz
                        -DOUTPUT=${TEST_OUT}/c-search-c-prefix-only.txt
                        -DPATTERN=co64
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_c_prefix_only PROPERTIES
                DEPENDS c_encode_prefix_only_search)
            add_test(NAME compare_c_prefix_only
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${PREFIX_EXPECTED}
                        -DACTUAL=${TEST_OUT}/c-search-c-prefix-only.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_prefix_only PROPERTIES
                DEPENDS "make_prefix_only_input;c_search_c_prefix_only")

            add_test(NAME go_encode_prefix_only_search
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        encode-search-prefix-only
                        ${PREFIX_INPUT} ${TEST_OUT}/go-prefix-only.mz co64 2)
            set_tests_properties(go_encode_prefix_only_search PROPERTIES
                DEPENDS make_prefix_only_input
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_prefix_only
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-prefix-only.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-prefix-only.txt
                        -DPATTERN=co64
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_prefix_only PROPERTIES
                DEPENDS go_encode_prefix_only_search)
            add_test(NAME compare_go_prefix_only
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${PREFIX_EXPECTED}
                        -DACTUAL=${TEST_OUT}/c-search-go-prefix-only.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_prefix_only PROPERTIES
                DEPENDS "make_prefix_only_input;c_search_go_prefix_only")

            add_test(NAME go_build_prefix_only_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        build-sidecar-prefix-only
                        ${TEST_OUT}/go-prefix-only.mz
                        ${TEST_OUT}/go-prefix-only.mzs co64)
            set_tests_properties(go_build_prefix_only_sidecar PROPERTIES
                DEPENDS go_encode_prefix_only_search
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_prefix_only_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-prefix-only.mz
                        -DSIDECAR=${TEST_OUT}/go-prefix-only.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-go-prefix-only-sidecar.txt
                        -DPATTERN=co64
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_prefix_only_sidecar PROPERTIES
                DEPENDS go_build_prefix_only_sidecar)
            add_test(NAME compare_go_prefix_only_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${PREFIX_EXPECTED}
                        -DACTUAL=${TEST_OUT}/c-search-go-prefix-only-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_prefix_only_sidecar PROPERTIES
                DEPENDS "make_prefix_only_input;c_search_go_prefix_only_sidecar")

            add_test(NAME c_build_prefix_only_sidecar
                COMMAND mz sidecar build -search.prefix co64
                        -o ${TEST_OUT}/c-prefix-only.mzs
                        ${TEST_OUT}/go-prefix-only.mz)
            set_tests_properties(c_build_prefix_only_sidecar PROPERTIES
                DEPENDS go_encode_prefix_only_search)
            add_test(NAME c_search_c_prefix_only_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-prefix-only.mz
                        -DSIDECAR=${TEST_OUT}/c-prefix-only.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-c-prefix-only-sidecar.txt
                        -DPATTERN=co64
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_c_prefix_only_sidecar PROPERTIES
                DEPENDS c_build_prefix_only_sidecar)
            add_test(NAME compare_c_prefix_only_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${PREFIX_EXPECTED}
                        -DACTUAL=${TEST_OUT}/c-search-c-prefix-only-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_prefix_only_sidecar PROPERTIES
                DEPENDS "make_prefix_only_input;c_search_c_prefix_only_sidecar")

            add_test(NAME c_encode_search_stream
                COMMAND mz c -q -bs 4096 -search -search.uncompressed
                        -o ${TEST_OUT}/c-search.mz ${TEST_TXT})
            add_test(NAME go_search_c_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-stream
                        ${TEST_OUT}/c-search.mz ${TEST_OUT}/go-search-c.txt Tom)
            set_tests_properties(go_search_c_stream PROPERTIES
                DEPENDS c_encode_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_c_stream
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/c-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-c.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_c_stream PROPERTIES
                DEPENDS c_encode_search_stream)
            add_test(NAME compare_c_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c.txt
                        -DACTUAL=${TEST_OUT}/c-search-c.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_search_stream PROPERTIES
                DEPENDS "go_search_c_stream;c_search_c_stream")

            add_test(NAME c_encode_compressed_search_stream
                COMMAND mz c -q -bs 4096 -search
                        -o ${TEST_OUT}/c-search-compressed.mz ${TEST_TXT})
            add_test(NAME go_search_c_compressed_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-stream
                        ${TEST_OUT}/c-search-compressed.mz
                        ${TEST_OUT}/go-search-c-compressed.txt Tom)
            set_tests_properties(go_search_c_compressed_stream PROPERTIES
                DEPENDS c_encode_compressed_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_compressed_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c.txt
                        -DACTUAL=${TEST_OUT}/go-search-c-compressed.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_compressed_search_stream PROPERTIES
                DEPENDS "go_search_c_stream;go_search_c_compressed_stream")

            add_test(NAME go_encode_search_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} encode-search-stream
                        ${TEST_TXT} ${TEST_OUT}/go-search.mz 2)
            set_tests_properties(go_encode_search_stream PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_nonmatch_nonquiet_succeeds
                COMMAND mz search
                        __minlz_v120_absent_pattern_7f8e9d__
                        ${TEST_OUT}/go-search.mz)
            set_tests_properties(c_search_nonmatch_nonquiet_succeeds PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME c_search_nonmatch_quiet_returns_one
                COMMAND mz search -q
                        __minlz_v120_absent_pattern_7f8e9d__
                        ${TEST_OUT}/go-search.mz)
            set_tests_properties(c_search_nonmatch_quiet_returns_one PROPERTIES
                DEPENDS go_encode_search_stream
                WILL_FAIL TRUE)
            add_test(NAME c_search_go_stream
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_stream PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_search_go_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-stream
                        ${TEST_OUT}/go-search.mz
                        ${TEST_OUT}/go-search-go.txt Tom)
            set_tests_properties(go_search_go_stream PROPERTIES
                DEPENDS go_encode_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_go_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go.txt
                        -DACTUAL=${TEST_OUT}/c-search-go.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_search_stream PROPERTIES
                DEPENDS "c_search_go_stream;go_search_go_stream")

            add_test(NAME c_search_go_stream_lines
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-lines.txt
                        -DPATTERN=Tom
                        -DLINES=ON
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_stream_lines PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_cli_search_go_stream_lines
                COMMAND ${CMAKE_COMMAND}
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/go-search-go-lines.txt
                        -DPATTERN=Tom
                        -P ${GO_SEARCH_CAPTURE})
            set_tests_properties(go_cli_search_go_stream_lines PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME compare_go_search_stream_lines
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go-lines.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-lines.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_search_stream_lines PROPERTIES
                DEPENDS "c_search_go_stream_lines;go_cli_search_go_stream_lines")

            set(SEARCH_MODE_CAPTURE
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_search_mode.cmake)
            add_test(NAME c_search_go_stream_blocks
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=c
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-blocks.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_MODE_CAPTURE})
            set_tests_properties(c_search_go_stream_blocks PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_cli_search_go_stream_blocks
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=go
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/go-search-go-blocks.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_MODE_CAPTURE})
            set_tests_properties(go_cli_search_go_stream_blocks PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME compare_go_search_stream_blocks
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go-blocks.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-blocks.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_search_stream_blocks PROPERTIES
                DEPENDS "c_search_go_stream_blocks;go_cli_search_go_stream_blocks")

            set(SEARCH_CLI_CAPTURE
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_search_cli.cmake)
            add_test(NAME c_search_go_stream_line_numbers
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=c
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-numbered.txt
                        -DPATTERN=Tom
                        -DEXTRA_ARGS=-n
                        -P ${SEARCH_CLI_CAPTURE})
            set_tests_properties(c_search_go_stream_line_numbers PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_cli_search_go_stream_line_numbers
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=go
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/go-search-go-numbered.txt
                        -DPATTERN=Tom
                        -DEXTRA_ARGS=-n
                        -P ${SEARCH_CLI_CAPTURE})
            set_tests_properties(go_cli_search_go_stream_line_numbers PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME compare_go_search_stream_line_numbers
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go-numbered.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-numbered.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_search_stream_line_numbers PROPERTIES
                DEPENDS "c_search_go_stream_line_numbers;go_cli_search_go_stream_line_numbers")

            add_test(NAME c_search_go_stream_multi_count
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=c
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DINPUT2=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-multi-count.txt
                        -DPATTERN=Tom
                        -DEXTRA_ARGS=-c
                        -P ${SEARCH_CLI_CAPTURE})
            set_tests_properties(c_search_go_stream_multi_count PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_cli_search_go_stream_multi_count
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=go
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DINPUT2=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/go-search-go-multi-count.txt
                        -DPATTERN=Tom
                        -DEXTRA_ARGS=-c
                        -P ${SEARCH_CLI_CAPTURE})
            set_tests_properties(go_cli_search_go_stream_multi_count PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME compare_go_search_stream_multi_count
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go-multi-count.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-multi-count.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_search_stream_multi_count PROPERTIES
                DEPENDS "c_search_go_stream_multi_count;go_cli_search_go_stream_multi_count")

            add_test(NAME c_encode_single_byte_search_prefix
                COMMAND mz c -q -2 -bs 4096 -no-index
                        -search -search.prefix ":"
                        -o ${TEST_OUT}/c-single-byte-prefix.mz ${TEST_TXT})
            add_test(NAME c_assert_single_byte_search_prefix
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/c-single-byte-prefix.mz
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_search_info.cmake)
            set_tests_properties(c_assert_single_byte_search_prefix PROPERTIES
                DEPENDS c_encode_single_byte_search_prefix)

            add_test(NAME go_encode_compressed_search_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        encode-search-stream-compressed
                        ${TEST_TXT} ${TEST_OUT}/go-search-compressed.mz 2)
            set_tests_properties(go_encode_compressed_search_stream PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_compressed_stream
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search-compressed.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-compressed.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_compressed_stream PROPERTIES
                DEPENDS go_encode_compressed_search_stream)
            add_test(NAME compare_go_compressed_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-compressed.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_compressed_search_stream PROPERTIES
                DEPENDS "go_search_go_stream;c_search_go_compressed_stream")

            function(minlz_add_go_search_config_case case_name kind match_len prefix extras)
                set(stream_file ${TEST_OUT}/go-search-${case_name}.mz)
                if(kind STREQUAL "long")
                    add_test(NAME go_encode_search_config_${case_name}
                        COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                                encode-search-config
                                ${TEST_TXT} ${stream_file}
                                ${kind} ${match_len} "${prefix}" ${extras})
                else()
                    add_test(NAME go_encode_search_config_${case_name}
                        COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                                encode-search-config
                                ${TEST_TXT} ${stream_file}
                                ${kind} ${match_len} "${prefix}")
                endif()
                set_tests_properties(go_encode_search_config_${case_name}
                    PROPERTIES WORKING_DIRECTORY ${MINLZ_GO_REPO})
                add_test(NAME c_search_go_config_${case_name}
                    COMMAND ${CMAKE_COMMAND}
                            -DMZ=$<TARGET_FILE:mz>
                            -DINPUT=${stream_file}
                            -DOUTPUT=${TEST_OUT}/c-search-go-${case_name}.txt
                            "-DPATTERN=Tom Sawyer"
                            -P ${SEARCH_CAPTURE})
                set_tests_properties(c_search_go_config_${case_name}
                    PROPERTIES DEPENDS go_encode_search_config_${case_name})
                add_test(NAME go_search_go_config_${case_name}
                    COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                            search-stream ${stream_file}
                            ${TEST_OUT}/go-search-go-${case_name}.txt
                            "Tom Sawyer")
                set_tests_properties(go_search_go_config_${case_name}
                    PROPERTIES
                        DEPENDS go_encode_search_config_${case_name}
                        WORKING_DIRECTORY ${MINLZ_GO_REPO})
                add_test(NAME compare_go_search_config_${case_name}
                    COMMAND ${CMAKE_COMMAND}
                            -DEXPECTED=${TEST_OUT}/go-search-go-${case_name}.txt
                            -DACTUAL=${TEST_OUT}/c-search-go-${case_name}.txt
                            -P ${SEARCH_COMPARE})
                set_tests_properties(compare_go_search_config_${case_name}
                    PROPERTIES
                        DEPENDS "c_search_go_config_${case_name};go_search_go_config_${case_name}")
            endfunction()

            minlz_add_go_search_config_case(byte byte 6 "T" 0)
            minlz_add_go_search_config_case(mask mask 6 "ABCDEFGTQ" 0)
            minlz_add_go_search_config_case(long long 4 "Tom " 2)

            function(minlz_add_c_search_config_case case_name)
                set(stream_file ${TEST_OUT}/c-search-${case_name}.mz)
                if(case_name STREQUAL "byte")
                    add_test(NAME c_encode_search_config_${case_name}
                        COMMAND mz c -q -bs 4096 -search
                                -search.len 6 -search.prefix "T"
                                -o ${stream_file} ${TEST_TXT})
                elseif(case_name STREQUAL "mask")
                    add_test(NAME c_encode_search_config_${case_name}
                        COMMAND mz c -q -bs 4096 -search
                                -search.len 6
                                -search.prefixes "ABCDEFGTQ"
                                -o ${stream_file} ${TEST_TXT})
                else()
                    add_test(NAME c_encode_search_config_${case_name}
                        COMMAND mz c -q -bs 4096 -search
                                -search.len 4 -search.prefix "Tom "
                                -search.extras 2
                                -o ${stream_file} ${TEST_TXT})
                endif()
                add_test(NAME c_search_c_config_${case_name}
                    COMMAND ${CMAKE_COMMAND}
                            -DMZ=$<TARGET_FILE:mz>
                            -DINPUT=${stream_file}
                            -DOUTPUT=${TEST_OUT}/c-search-c-${case_name}.txt
                            "-DPATTERN=Tom Sawyer"
                            -P ${SEARCH_CAPTURE})
                set_tests_properties(c_search_c_config_${case_name}
                    PROPERTIES DEPENDS c_encode_search_config_${case_name})
                add_test(NAME go_search_c_config_${case_name}
                    COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                            search-stream ${stream_file}
                            ${TEST_OUT}/go-search-c-${case_name}.txt
                            "Tom Sawyer")
                set_tests_properties(go_search_c_config_${case_name}
                    PROPERTIES
                        DEPENDS c_encode_search_config_${case_name}
                        WORKING_DIRECTORY ${MINLZ_GO_REPO})
                add_test(NAME compare_c_search_config_${case_name}
                    COMMAND ${CMAKE_COMMAND}
                            -DEXPECTED=${TEST_OUT}/go-search-c-${case_name}.txt
                            -DACTUAL=${TEST_OUT}/c-search-c-${case_name}.txt
                            -P ${SEARCH_COMPARE})
                set_tests_properties(compare_c_search_config_${case_name}
                    PROPERTIES
                        DEPENDS "c_search_c_config_${case_name};go_search_c_config_${case_name}")
            endfunction()

            minlz_add_c_search_config_case(byte)
            minlz_add_c_search_config_case(mask)
            minlz_add_c_search_config_case(long)

            add_test(NAME compat_randomized_search_matrix
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        verify-search-matrix
                        ${TEST_TXT}
                        $<TARGET_FILE:mz>
                        ${TEST_OUT}/search-matrix)
            set_tests_properties(compat_randomized_search_matrix PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO}
                TIMEOUT 180)

            add_test(NAME go_encode_huff0_search_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        encode-search-huff0-stream
                        ${TEST_TXT} ${TEST_OUT}/go-search-huff0.mz)
            set_tests_properties(go_encode_huff0_search_stream PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_huff0_stream_bail
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search-huff0.mz
                        -DOUTPUT=${TEST_OUT}/c-search-go-huff0.txt
                        -DPATTERN=:HUFF0-NEEDLE
                        -DBAIL=1
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_huff0_stream_bail PROPERTIES
                DEPENDS go_encode_huff0_search_stream)
            add_test(NAME compare_go_huff0_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_huff0_search.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-huff0.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_huff0_search_stream PROPERTIES
                DEPENDS c_search_go_huff0_stream_bail)

            add_test(NAME go_decode_huff0_fixture
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        decode-stream
                        ${TEST_OUT}/go-search-huff0.mz
                        ${TEST_OUT}/huff0-fixture.raw)
            set_tests_properties(go_decode_huff0_fixture PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO}
                DEPENDS go_encode_huff0_search_stream)
            add_test(NAME c_encode_huff0_search_stream
                COMMAND mz c -q -2 -bs 1M
                        -search -search.len 6 -search.prefixes ":"
                        -o ${TEST_OUT}/c-search-huff0.mz
                        ${TEST_OUT}/huff0-fixture.raw)
            set_tests_properties(c_encode_huff0_search_stream PROPERTIES
                DEPENDS go_decode_huff0_fixture)
            add_test(NAME assert_c_huff0_search_stream
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        assert-huff0-stream
                        ${TEST_OUT}/c-search-huff0.mz ignored)
            set_tests_properties(assert_c_huff0_search_stream PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO}
                DEPENDS c_encode_huff0_search_stream)
            add_test(NAME go_search_c_huff0_stream_bail
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT}
                        search-stream
                        ${TEST_OUT}/c-search-huff0.mz
                        ${TEST_OUT}/go-search-c-huff0.txt
                        :HUFF0-NEEDLE bail)
            set_tests_properties(go_search_c_huff0_stream_bail PROPERTIES
                WORKING_DIRECTORY ${MINLZ_GO_REPO}
                DEPENDS assert_c_huff0_search_stream)
            add_test(NAME compare_c_huff0_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_huff0_search.txt
                        -DACTUAL=${TEST_OUT}/go-search-c-huff0.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_huff0_search_stream PROPERTIES
                DEPENDS go_search_c_huff0_stream_bail)

            add_test(NAME c_build_sidecar
                COMMAND mz sidecar build -o ${TEST_OUT}/c-sidecar.mzs
                        ${TEST_OUT}/go-search.mz)
            set_tests_properties(c_build_sidecar PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME go_search_c_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-sidecar
                        ${TEST_OUT}/go-search.mz
                        ${TEST_OUT}/go-search-c-sidecar.txt
                        ${TEST_OUT}/c-sidecar.mzs Tom)
            set_tests_properties(go_search_c_sidecar PROPERTIES
                DEPENDS c_build_sidecar
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go.txt
                        -DACTUAL=${TEST_OUT}/go-search-c-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_sidecar PROPERTIES
                DEPENDS "go_search_go_stream;go_search_c_sidecar")

            add_test(NAME c_search_help
                COMMAND mz search -help)
            add_test(NAME c_sidecar_help
                COMMAND mz sidecar -help)
            add_test(NAME c_sidecar_build_help
                COMMAND mz sidecar build -help)
            add_test(NAME c_sidecar_extract_help
                COMMAND mz sidecar extract -help)
            add_test(NAME prepare_sidecar_missing_input
                COMMAND ${CMAKE_COMMAND} -E rm -f
                        ${TEST_OUT}/missing-input.mz.mzs)
            add_test(NAME c_sidecar_missing_input_fails
                COMMAND mz sidecar build
                        ${TEST_OUT}/missing-input.mz)
            set_tests_properties(c_sidecar_missing_input_fails PROPERTIES
                DEPENDS prepare_sidecar_missing_input
                WILL_FAIL TRUE)
            add_test(NAME c_sidecar_missing_input_leaves_no_output
                COMMAND ${CMAKE_COMMAND}
                        -DPATH_TO_CHECK=${TEST_OUT}/missing-input.mz.mzs
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_not_exists.cmake)
            set_tests_properties(c_sidecar_missing_input_leaves_no_output PROPERTIES
                DEPENDS c_sidecar_missing_input_fails)

            add_test(NAME prepare_sidecar_collision_input
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${TEST_OUT}/go-search.mz
                        ${TEST_OUT}/sidecar-collision.mz)
            set_tests_properties(prepare_sidecar_collision_input PROPERTIES
                DEPENDS go_encode_search_stream)
            add_test(NAME c_sidecar_build_refuses_input_overwrite
                COMMAND mz sidecar build
                        -o ${TEST_OUT}/sidecar-collision.mz
                        ${TEST_OUT}/sidecar-collision.mz)
            set_tests_properties(c_sidecar_build_refuses_input_overwrite PROPERTIES
                DEPENDS prepare_sidecar_collision_input
                WILL_FAIL TRUE)
            add_test(NAME c_sidecar_extract_refuses_newstream_collision
                COMMAND mz sidecar extract
                        -o ${TEST_OUT}/sidecar-collision.mzs
                        -newstream ${TEST_OUT}/sidecar-collision.mzs
                        ${TEST_OUT}/sidecar-collision.mz)
            set_tests_properties(c_sidecar_extract_refuses_newstream_collision PROPERTIES
                DEPENDS prepare_sidecar_collision_input
                WILL_FAIL TRUE)
            add_test(NAME c_sidecar_build_refuses_newstream
                COMMAND mz sidecar build
                        -o ${TEST_OUT}/sidecar-build-invalid.mzs
                        -newstream ${TEST_OUT}/sidecar-build-invalid.mz
                        ${TEST_OUT}/sidecar-collision.mz)
            set_tests_properties(c_sidecar_build_refuses_newstream PROPERTIES
                DEPENDS prepare_sidecar_collision_input
                WILL_FAIL TRUE)

            add_test(NAME go_build_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} build-sidecar
                        ${TEST_OUT}/go-search.mz
                        ${TEST_OUT}/go-sidecar.mzs)
            set_tests_properties(go_build_sidecar PROPERTIES
                DEPENDS go_encode_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DSIDECAR=${TEST_OUT}/go-sidecar.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-go-sidecar.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_sidecar PROPERTIES
                DEPENDS go_build_sidecar)
            add_test(NAME compare_go_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-go.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_sidecar PROPERTIES
                DEPENDS "go_search_go_stream;c_search_go_sidecar")

            add_test(NAME make_concatenated_search_stream
                COMMAND ${CMAKE_COMMAND}
                        -DINPUT=${TEST_OUT}/go-search.mz
                        -DOUTPUT=${TEST_OUT}/go-search-concat.mz
                        -P ${CONCAT_FILES})
            set_tests_properties(make_concatenated_search_stream PROPERTIES
                DEPENDS go_encode_search_stream)

            add_test(NAME c_build_concatenated_sidecar
                COMMAND mz sidecar build
                        -o ${TEST_OUT}/c-concat-sidecar.mzs
                        ${TEST_OUT}/go-search-concat.mz)
            set_tests_properties(c_build_concatenated_sidecar PROPERTIES
                DEPENDS make_concatenated_search_stream)
            add_test(NAME go_search_c_concatenated_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-sidecar
                        ${TEST_OUT}/go-search-concat.mz
                        ${TEST_OUT}/go-search-c-concat-sidecar.txt
                        ${TEST_OUT}/c-concat-sidecar.mzs Tom)
            set_tests_properties(go_search_c_concatenated_sidecar PROPERTIES
                DEPENDS c_build_concatenated_sidecar
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_c_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search-concat.mz
                        -DSIDECAR=${TEST_OUT}/c-concat-sidecar.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-c-concat-sidecar.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_c_concatenated_sidecar PROPERTIES
                DEPENDS c_build_concatenated_sidecar)
            add_test(NAME compare_c_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c-concat-sidecar.txt
                        -DACTUAL=${TEST_OUT}/c-search-c-concat-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_concatenated_sidecar PROPERTIES
                DEPENDS "go_search_c_concatenated_sidecar;c_search_c_concatenated_sidecar")

            add_test(NAME go_build_concatenated_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} build-sidecar
                        ${TEST_OUT}/go-search-concat.mz
                        ${TEST_OUT}/go-concat-sidecar.mzs)
            set_tests_properties(go_build_concatenated_sidecar PROPERTIES
                DEPENDS make_concatenated_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-search-concat.mz
                        -DSIDECAR=${TEST_OUT}/go-concat-sidecar.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-go-concat-sidecar.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_concatenated_sidecar PROPERTIES
                DEPENDS go_build_concatenated_sidecar)
            add_test(NAME compare_go_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c-concat-sidecar.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-concat-sidecar.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_concatenated_sidecar PROPERTIES
                DEPENDS "go_search_c_concatenated_sidecar;c_search_go_concatenated_sidecar")

            add_test(NAME c_extract_concatenated_sidecar
                COMMAND mz sidecar extract
                        -o ${TEST_OUT}/c-extracted-concat-sidecar.mzs
                        -newstream ${TEST_OUT}/c-stripped-concat.mz
                        ${TEST_OUT}/go-search-concat.mz)
            set_tests_properties(c_extract_concatenated_sidecar PROPERTIES
                DEPENDS make_concatenated_search_stream)
            add_test(NAME go_search_c_extracted_concatenated_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-sidecar
                        ${TEST_OUT}/c-stripped-concat.mz
                        ${TEST_OUT}/go-search-c-extracted-concat.txt
                        ${TEST_OUT}/c-extracted-concat-sidecar.mzs Tom)
            set_tests_properties(go_search_c_extracted_concatenated_sidecar PROPERTIES
                DEPENDS c_extract_concatenated_sidecar
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME compare_c_extracted_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c-concat-sidecar.txt
                        -DACTUAL=${TEST_OUT}/go-search-c-extracted-concat.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_c_extracted_concatenated_sidecar PROPERTIES
                DEPENDS "go_search_c_concatenated_sidecar;go_search_c_extracted_concatenated_sidecar")

            add_test(NAME go_extract_concatenated_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} extract-sidecar
                        ${TEST_OUT}/go-search-concat.mz
                        ${TEST_OUT}/go-extracted-concat-sidecar.mzs
                        ${TEST_OUT}/go-stripped-concat.mz)
            set_tests_properties(go_extract_concatenated_sidecar PROPERTIES
                DEPENDS make_concatenated_search_stream
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_go_extracted_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/go-stripped-concat.mz
                        -DSIDECAR=${TEST_OUT}/go-extracted-concat-sidecar.mzs
                        -DOUTPUT=${TEST_OUT}/c-search-go-extracted-concat.txt
                        -DPATTERN=Tom
                        -P ${SEARCH_CAPTURE})
            set_tests_properties(c_search_go_extracted_concatenated_sidecar PROPERTIES
                DEPENDS go_extract_concatenated_sidecar)
            add_test(NAME compare_go_extracted_concatenated_sidecar
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-search-c-concat-sidecar.txt
                        -DACTUAL=${TEST_OUT}/c-search-go-extracted-concat.txt
                        -P ${SEARCH_COMPARE})
            set_tests_properties(compare_go_extracted_concatenated_sidecar PROPERTIES
                DEPENDS "go_search_c_concatenated_sidecar;c_search_go_extracted_concatenated_sidecar")

            set(STATS_CAPTURE ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture_stats.cmake)
            set(STATS_COMPARE ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_stats_csv.cmake)
            add_test(NAME c_make_stats_block
                COMMAND mz c -q -2 -block
                        -o ${TEST_OUT}/stats-c.mzb ${TEST_TXT})
            add_test(NAME c_stats_raw_csv
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=c
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/stats-c.mzb
                        -DOUTPUT=${TEST_OUT}/c-stats-raw.csv
                        -P ${STATS_CAPTURE})
            set_tests_properties(c_stats_raw_csv PROPERTIES
                DEPENDS c_make_stats_block)
            add_test(NAME go_stats_raw_csv
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=go
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/stats-c.mzb
                        -DOUTPUT=${TEST_OUT}/go-stats-raw.csv
                        -P ${STATS_CAPTURE})
            set_tests_properties(go_stats_raw_csv PROPERTIES
                DEPENDS c_make_stats_block)
            add_test(NAME compare_raw_stats
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-stats-raw.csv
                        -DACTUAL=${TEST_OUT}/c-stats-raw.csv
                        -P ${STATS_COMPARE})
            set_tests_properties(compare_raw_stats PROPERTIES
                DEPENDS "c_stats_raw_csv;go_stats_raw_csv")

            add_test(NAME c_make_stats_stream
                COMMAND mz c -q -2 -no-index
                        -o ${TEST_OUT}/stats-c.mz ${TEST_TXT})
            add_test(NAME c_stats_stream_csv
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=c
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/stats-c.mz
                        -DOUTPUT=${TEST_OUT}/c-stats-stream.csv
                        -P ${STATS_CAPTURE})
            set_tests_properties(c_stats_stream_csv PROPERTIES
                DEPENDS c_make_stats_stream)
            add_test(NAME go_stats_stream_csv
                COMMAND ${CMAKE_COMMAND}
                        -DMODE=go
                        -DGO=${GO_EXECUTABLE}
                        -DGO_REPO=${MINLZ_GO_REPO}
                        -DINPUT=${TEST_OUT}/stats-c.mz
                        -DOUTPUT=${TEST_OUT}/go-stats-stream.csv
                        -P ${STATS_CAPTURE})
            set_tests_properties(go_stats_stream_csv PROPERTIES
                DEPENDS c_make_stats_stream)
            add_test(NAME compare_stream_stats
                COMMAND ${CMAKE_COMMAND}
                        -DEXPECTED=${TEST_OUT}/go-stats-stream.csv
                        -DACTUAL=${TEST_OUT}/c-stats-stream.csv
                        -P ${STATS_COMPARE})
            set_tests_properties(compare_stream_stats PROPERTIES
                DEPENDS "c_stats_stream_csv;go_stats_stream_csv")
            add_test(NAME c_stats_stream_json
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/stats-c.mz
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_stats_json.cmake)
            set_tests_properties(c_stats_stream_json PROPERTIES
                DEPENDS c_make_stats_stream)

            add_test(NAME c_encode_auto_search_sidecar
                COMMAND mz c -q -bs 4096
                        -search -search.compress.skip 0
                        -search.sidecar
                        -o ${TEST_OUT}/c-auto-side.mz ${TEST_TXT})
            add_test(NAME go_search_c_auto_sidecar
                COMMAND ${GO_EXECUTABLE} run ${GO_COMPAT} search-sidecar
                        ${TEST_OUT}/c-auto-side.mz
                        ${TEST_OUT}/go-search-c-auto-side.txt
                        ${TEST_OUT}/c-auto-side.mz.mzs Tom)
            set_tests_properties(go_search_c_auto_sidecar PROPERTIES
                DEPENDS c_encode_auto_search_sidecar
                WORKING_DIRECTORY ${MINLZ_GO_REPO})
            add_test(NAME c_search_window_stats
                COMMAND ${CMAKE_COMMAND}
                        -DMZ=$<TARGET_FILE:mz>
                        -DINPUT=${TEST_OUT}/c-auto-side.mz
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_window_stats.cmake)
            set_tests_properties(c_search_window_stats PROPERTIES
                DEPENDS c_encode_auto_search_sidecar)
        else()
            message(STATUS "Go compatibility CTests disabled: Go executable, bundled helper, or ${MINLZ_GO_REPO}/go.mod not found")
        endif()
    endif()
endif()

install(TARGETS minlz EXPORT minlzTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(MINLZ_BUILD_CLI)
    install(TARGETS mz RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES include/minlz.h DESTINATION ${CMAKE_INSTALL_DOCDIR}/include)
install(FILES
    Readme.md
    Readme.ru.md
    LICENSE
    THIRD_PARTY_NOTICES.md
    THIRD_PARTY_NOTICES.ru.md
    SPEC.md
    SPEC_SEARCH.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES
    docs/COMPATIBILITY.md
    docs/COMPATIBILITY.ru.md
    docs/DEVELOPMENT.md
    docs/DEVELOPMENT.ru.md
    docs/OPTIMIZATION.md
    docs/OPTIMIZATION.ru.md
    docs/PERFORMANCE.md
    docs/PERFORMANCE.ru.md
    docs/RELEASE_CHECKLIST.md
    docs/RELEASE_CHECKLIST.ru.md
    docs/SEARCH.md
    docs/SEARCH.ru.md
    docs/TESTING.md
    docs/TESTING.ru.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
)
install(EXPORT minlzTargets NAMESPACE minlz:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minlz)

configure_package_config_file(
    cmake/minlzConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/minlzConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minlz
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/minlzConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/minlzConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/minlzConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minlz
)


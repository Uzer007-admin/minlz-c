if(NOT DEFINED MODE OR NOT DEFINED INPUT OR
   NOT DEFINED OUTPUT OR NOT DEFINED PATTERN)
    message(FATAL_ERROR
        "MODE, INPUT, OUTPUT, and PATTERN are required")
endif()

set(command_args search)
if(DEFINED EXTRA_ARGS AND NOT EXTRA_ARGS STREQUAL "")
    list(APPEND command_args ${EXTRA_ARGS})
endif()
list(APPEND command_args "${PATTERN}" "${INPUT}")
if(DEFINED INPUT2 AND NOT INPUT2 STREQUAL "")
    list(APPEND command_args "${INPUT2}")
endif()

if(MODE STREQUAL "c")
    if(NOT DEFINED MZ)
        message(FATAL_ERROR "MZ is required for MODE=c")
    endif()
    execute_process(
        COMMAND "${MZ}" ${command_args}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE matches
        ERROR_VARIABLE search_error)
elseif(MODE STREQUAL "go")
    if(NOT DEFINED GO OR NOT DEFINED GO_REPO)
        message(FATAL_ERROR
            "GO and GO_REPO are required for MODE=go")
    endif()
    execute_process(
        COMMAND "${GO}" run ./cmd/mz ${command_args}
        WORKING_DIRECTORY "${GO_REPO}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE matches
        ERROR_VARIABLE search_error)
else()
    message(FATAL_ERROR "unknown MODE=${MODE}")
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "search failed with exit code ${result}: ${search_error}")
endif()
string(ASCII 13 carriage_return)
string(REPLACE "${carriage_return}" "" matches "${matches}")
file(WRITE "${OUTPUT}" "${matches}")

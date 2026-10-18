if(NOT DEFINED MODE OR NOT DEFINED INPUT OR
   NOT DEFINED OUTPUT OR NOT DEFINED PATTERN)
    message(FATAL_ERROR "MODE, INPUT, OUTPUT, and PATTERN are required")
endif()

if(MODE STREQUAL "c")
    if(NOT DEFINED MZ)
        message(FATAL_ERROR "MZ is required for MODE=c")
    endif()
    execute_process(
        COMMAND "${MZ}" search -l=false "${PATTERN}" "${INPUT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE matches
        ERROR_VARIABLE search_error)
elseif(MODE STREQUAL "go")
    if(NOT DEFINED GO OR NOT DEFINED GO_REPO)
        message(FATAL_ERROR "GO and GO_REPO are required for MODE=go")
    endif()
    execute_process(
        COMMAND "${GO}" run ./cmd/mz search -l=false "${PATTERN}" "${INPUT}"
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
string(REPLACE "\r\n" "\n" matches "${matches}")
file(WRITE "${OUTPUT}" "${matches}")

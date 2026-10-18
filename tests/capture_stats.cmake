if(NOT DEFINED MODE OR NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "MODE, INPUT and OUTPUT are required")
endif()

if(MODE STREQUAL "c")
    if(NOT DEFINED MZ)
        message(FATAL_ERROR "MZ is required for MODE=c")
    endif()
    execute_process(
        COMMAND "${MZ}" stats -csv -per-block "${INPUT}"
        RESULT_VARIABLE result
        OUTPUT_FILE "${OUTPUT}"
        ERROR_VARIABLE error_text)
elseif(MODE STREQUAL "go")
    if(NOT DEFINED GO OR NOT DEFINED GO_REPO)
        message(FATAL_ERROR "GO and GO_REPO are required for MODE=go")
    endif()
    execute_process(
        COMMAND "${GO}" run ./cmd/mz stats -csv -per-block "${INPUT}"
        WORKING_DIRECTORY "${GO_REPO}"
        RESULT_VARIABLE result
        OUTPUT_FILE "${OUTPUT}"
        ERROR_VARIABLE error_text)
else()
    message(FATAL_ERROR "unknown MODE=${MODE}")
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "stats command failed (${result}): ${error_text}")
endif()

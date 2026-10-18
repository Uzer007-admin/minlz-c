if(NOT DEFINED GO OR NOT DEFINED GO_REPO OR
   NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR
   NOT DEFINED PATTERN)
    message(FATAL_ERROR
        "GO, GO_REPO, INPUT, OUTPUT, and PATTERN are required")
endif()

execute_process(
    COMMAND "${GO}" run ./cmd/mz search "${PATTERN}" "${INPUT}"
    WORKING_DIRECTORY "${GO_REPO}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE matches
    ERROR_VARIABLE search_error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "Go mz search failed with exit code ${result}: ${search_error}")
endif()

file(WRITE "${OUTPUT}" "${matches}")

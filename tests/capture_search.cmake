if(NOT DEFINED MZ OR NOT DEFINED INPUT OR
   NOT DEFINED OUTPUT OR NOT DEFINED PATTERN)
    message(FATAL_ERROR "MZ, INPUT, OUTPUT, and PATTERN are required")
endif()

set(command_args search)
if(NOT DEFINED LINES OR NOT LINES)
    list(APPEND command_args -offsets)
endif()
if(DEFINED BAIL AND BAIL)
    list(APPEND command_args -bail)
endif()
if(DEFINED SIDECAR AND NOT SIDECAR STREQUAL "")
    list(APPEND command_args -sidecar "${SIDECAR}")
endif()
list(APPEND command_args "${PATTERN}" "${INPUT}")

execute_process(
    COMMAND "${MZ}" ${command_args}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE matches
    ERROR_VARIABLE search_error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "mz search failed with exit code ${result}: ${search_error}")
endif()

string(ASCII 13 carriage_return)
string(REPLACE "${carriage_return}" "" matches "${matches}")
file(WRITE "${OUTPUT}" "${matches}")

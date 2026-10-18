if(NOT DEFINED MZ OR NOT DEFINED INPUT)
    message(FATAL_ERROR "MZ and INPUT are required")
endif()

execute_process(
    COMMAND "${MZ}" search -v -offsets Tom "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE search_info)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "verbose search failed with exit code ${result}: ${search_info}")
endif()
if(NOT search_info MATCHES "search info: .*byte-prefix=\\\":\\\"")
    message(FATAL_ERROR
        "single-byte -search.prefix did not create type-2 byte prefix:\n${search_info}")
endif()

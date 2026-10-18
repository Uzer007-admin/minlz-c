if(NOT DEFINED MZ OR NOT DEFINED INPUT)
    message(FATAL_ERROR "MZ and INPUT are required")
endif()

execute_process(
    COMMAND "${MZ}" stats -json -per-block "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE json
    ERROR_VARIABLE error_text)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "mz stats -json failed (${result}): ${error_text}")
endif()
foreach(required
        "\"files\""
        "\"totals\""
        "\"ops\""
        "\"blocks\""
        "\"chunkTypes\"")
    string(FIND "${json}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "JSON output is missing ${required}")
    endif()
endforeach()

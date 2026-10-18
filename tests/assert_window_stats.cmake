if(NOT DEFINED MZ OR NOT DEFINED INPUT)
    message(FATAL_ERROR "MZ and INPUT are required")
endif()

execute_process(
    COMMAND "${MZ}" search -q -window-stats Sawyer "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_text)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "window-stats search failed (${result}): ${error_text}")
endif()
foreach(required
        "Blocks total:"
        "Table population:"
        "Pattern windows")
    string(FIND "${error_text}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "window-stats output is missing ${required}")
    endif()
endforeach()

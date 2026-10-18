if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E cat "${INPUT}" "${INPUT}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${OUTPUT}"
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unable to concatenate ${INPUT}")
endif()

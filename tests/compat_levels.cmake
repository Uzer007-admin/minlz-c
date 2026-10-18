if(NOT DEFINED DIRECTION OR NOT DEFINED FORMAT OR
   NOT DEFINED LEVEL OR NOT DEFINED LEVEL_FLAG OR
   NOT DEFINED GO OR NOT DEFINED GO_REPO OR
   NOT DEFINED GO_COMPAT OR NOT DEFINED MZ OR
   NOT DEFINED INPUT OR NOT DEFINED OUT_DIR OR
   NOT DEFINED CASE_NAME)
    message(FATAL_ERROR "missing compatibility test argument")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
set(encoded "${OUT_DIR}/${CASE_NAME}.encoded")
set(decoded "${OUT_DIR}/${CASE_NAME}.decoded")

if(FORMAT STREQUAL "block")
    set(go_encode encode-block)
    set(go_decode decode-block)
    set(c_format -block)
elseif(FORMAT STREQUAL "stream")
    set(go_encode encode-stream)
    set(go_decode decode-stream)
    set(c_format)
else()
    message(FATAL_ERROR "unknown format: ${FORMAT}")
endif()

if(DIRECTION STREQUAL "go-to-c")
    execute_process(
        COMMAND "${GO}" run "${GO_COMPAT}" "${go_encode}"
                "${INPUT}" "${encoded}" "${LEVEL}"
        WORKING_DIRECTORY "${GO_REPO}"
        RESULT_VARIABLE encode_result
        ERROR_VARIABLE encode_error
    )
    if(NOT encode_result EQUAL 0)
        message(FATAL_ERROR "Go encode failed: ${encode_error}")
    endif()
    execute_process(
        COMMAND "${MZ}" d -q ${c_format} -o "${decoded}" "${encoded}"
        RESULT_VARIABLE decode_result
        ERROR_VARIABLE decode_error
    )
    if(NOT decode_result EQUAL 0)
        message(FATAL_ERROR "C decode failed: ${decode_error}")
    endif()
elseif(DIRECTION STREQUAL "c-to-go")
    execute_process(
        COMMAND "${MZ}" c -q ${c_format} "${LEVEL_FLAG}"
                -o "${encoded}" "${INPUT}"
        RESULT_VARIABLE encode_result
        ERROR_VARIABLE encode_error
    )
    if(NOT encode_result EQUAL 0)
        message(FATAL_ERROR "C encode failed: ${encode_error}")
    endif()
    execute_process(
        COMMAND "${GO}" run "${GO_COMPAT}" "${go_decode}"
                "${encoded}" "${decoded}"
        WORKING_DIRECTORY "${GO_REPO}"
        RESULT_VARIABLE decode_result
        ERROR_VARIABLE decode_error
    )
    if(NOT decode_result EQUAL 0)
        message(FATAL_ERROR "Go decode failed: ${decode_error}")
    endif()
else()
    message(FATAL_ERROR "unknown direction: ${DIRECTION}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${INPUT}" "${decoded}"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "${DIRECTION} ${FORMAT} level ${LEVEL} content mismatch")
endif()

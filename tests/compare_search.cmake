if(NOT DEFINED EXPECTED OR NOT DEFINED ACTUAL)
    message(FATAL_ERROR "EXPECTED and ACTUAL are required")
endif()

file(STRINGS "${EXPECTED}" expected_matches)
file(STRINGS "${ACTUAL}" actual_matches)
if(NOT expected_matches STREQUAL actual_matches)
    message(FATAL_ERROR
        "Search results differ:\nexpected=${expected_matches}\nactual=${actual_matches}")
endif()

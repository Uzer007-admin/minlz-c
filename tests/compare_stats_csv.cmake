if(NOT DEFINED EXPECTED OR NOT DEFINED ACTUAL)
    message(FATAL_ERROR "EXPECTED and ACTUAL are required")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${ACTUAL}" actual)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual "${actual}")

# The first CSV row contains the input path. Windows argv encoding can differ
# between the Go and MSVC runtimes; every semantic statistics row must match.
string(REGEX REPLACE "^[^\n]*\n" "" expected "${expected}")
string(REGEX REPLACE "^[^\n]*\n" "" actual "${actual}")

if(NOT expected STREQUAL actual)
    message(FATAL_ERROR "C and Go statistics differ")
endif()

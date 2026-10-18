if(NOT DEFINED OUTPUT OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "OUTPUT and EXPECTED are required")
endif()

# Three 4 KiB stream blocks. The only match begins in the unindexed tail of
# the final block, which exercises the MinLZ v1.2 prefix-only tail rescue.
string(REPEAT "x" 8996 body)
file(WRITE "${OUTPUT}" "${body}co64")
file(WRITE "${EXPECTED}" "8996\n")

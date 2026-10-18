if(NOT DEFINED PATH_TO_CHECK)
    message(FATAL_ERROR "PATH_TO_CHECK is required")
endif()

if(EXISTS "${PATH_TO_CHECK}")
    message(FATAL_ERROR "Unexpected file exists: ${PATH_TO_CHECK}")
endif()

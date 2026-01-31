# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles\\dracoved_app_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\dracoved_app_autogen.dir\\ParseCache.txt"
  "dracoved_app_autogen"
  )
endif()

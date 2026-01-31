# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles\\dracoved_qt_sanity_test_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\dracoved_qt_sanity_test_autogen.dir\\ParseCache.txt"
  "dracoved_qt_sanity_test_autogen"
  )
endif()

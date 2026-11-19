# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/Kites_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/Kites_autogen.dir/ParseCache.txt"
  "CMakeFiles/kites_cli_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/kites_cli_autogen.dir/ParseCache.txt"
  "CMakeFiles/kites_core_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/kites_core_autogen.dir/ParseCache.txt"
  "Kites_autogen"
  "kites_cli_autogen"
  "kites_core_autogen"
  )
endif()

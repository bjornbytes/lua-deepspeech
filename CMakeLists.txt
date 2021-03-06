cmake_minimum_required(VERSION 3.1.0)
project(lua-deepspeech)

add_library(lua-deepspeech MODULE lua_deepspeech.c)
set_target_properties(lua-deepspeech PROPERTIES PREFIX "")

if(DEEPSPEECH_PATH)
  add_library(deepspeech SHARED IMPORTED GLOBAL)
  target_include_directories(lua-deepspeech PRIVATE "${DEEPSPEECH_PATH}")
  target_link_libraries(lua-deepspeech PRIVATE deepspeech)
  if(WIN32)
    set_target_properties(deepspeech PROPERTIES IMPORTED_IMPLIB "${DEEPSPEECH_PATH}/libdeepspeech.so.if.lib")
    set_target_properties(deepspeech PROPERTIES IMPORTED_LOCATION "${DEEPSPEECH_PATH}/libdeepspeech.so")
  else()
    set_target_properties(deepspeech PROPERTIES IMPORTED_LOCATION "${DEEPSPEECH_PATH}/libdeepspeech.so")
  endif()
else()
  message(FATAL_ERROR "Deepspeech path not found.  Set DEEPSPEECH_PATH to the path to the deepspeech native client library.")
endif()

if(LOVR)
  set(LOVR_PLUGIN_TARGETS lua-deepspeech deepspeech)
else()
  include(FindLua)
  find_package(Lua REQUIRED)
  target_include_directories(lua-deepspeech PRIVATE "${LUA_INCLUDE_DIR}")
  target_link_libraries(lua-deepspeech PRIVATE "${LUA_LIBRARIES}")
endif()

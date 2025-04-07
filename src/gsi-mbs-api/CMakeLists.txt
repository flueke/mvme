################################################################################
# Copyright (C) 2014-2024 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  #
#                                                                              #
#              This software is distributed under the terms of the             #
#              GNU Lesser General Public Licence (LGPL) version 3,             #
#                  copied verbatim in the file "LICENSE"                       #
################################################################################

# Modified for mvme by f.lueke@mesytec.com, 4/2025

set(target gsi-mbs-api)

set(sources
  f_evt.c
  f_stccomm.c
  f_ut_utime.c
  fLmd.c
)

list(APPEND headers
  f_evt.h
  fLmd.h
  f_stccomm.h
  f_ut_utime.h
  portnum_def.h
  s_bufhe.h
  s_bufhe_swap.h
  s_evhe.h
  s_evhe_swap.h
  s_filhe.h
  s_filhe_swap.h
  sMbs.h
  s_stdint.h
  s_ve10_1.h
  s_ve10_1_swap.h
  s_ves10_1.h
  s_ves10_1_swap.h
  typedefs.h
)

add_library(${target} STATIC ${sources} ${headers})

target_compile_options(${target} PRIVATE
  $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-deprecated-declarations>
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-deprecated-non-prototype>
)

if(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
  target_compile_options(${target} PRIVATE
    "-Wno-deprecated-non-prototype"
  )
endif()

target_include_directories(${target} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

check_symbol_exists(CLOCK_REALTIME "time.h" HAS_CLOCK_REALTIME)
target_compile_definitions(${target} PRIVATE
  $<$<PLATFORM_ID:Linux>:Linux>
  $<$<PLATFORM_ID:Linux>:SYSTEM64>
  $<$<PLATFORM_ID:Linux>:_LARGEFILE64_SOURCE>
  $<$<PLATFORM_ID:Darwin>:Darwin>
  $<$<PLATFORM_ID:Darwin>:SYSTEM64>
  $<$<PLATFORM_ID:Darwin>:_LARGEFILE64_SOURCE>
  $<$<BOOL:${HAS_CLOCK_REALTIME}>:_HAS_CLOCK_REALTIME>
  #GSI_LINUX
  Linux
)

#fairroot_install_exported(TARGETS ${target})
#install(FILES ${headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

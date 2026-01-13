include_directories(${e2sim_INCLUDE_DIRS})
# Use relative paths from ns-3 root to oran-e2sim (sibling directory)
# Get absolute path to avoid CMake INTERFACE_INCLUDE_DIRECTORIES restrictions
get_filename_component(ORAN_E2SIM_DIR "${CMAKE_SOURCE_DIR}/../oran-e2sim" ABSOLUTE)

# Add oran-e2sim include directories globally so any target that includes oran-interface headers can find them
# This is needed because oran-interface headers include oran-e2sim headers, and other modules (like netanim) 
# that include oran-interface headers need access to these directories
include_directories(
  ${ORAN_E2SIM_DIR}/e2sim/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/e2sm_examples/kpm_e2sm/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/src/base
  ${ORAN_E2SIM_DIR}/e2sim/src/encoding
)

build_lib(
    LIBNAME oran-interface
    SOURCE_FILES model/oran-interface.cc
                 model/asn1c-types.cc
                 model/function-description.cc
                 model/kpm-indication.cc
                 model/kpm-function-description.cc
                 model/ric-control-message.cc
                 model/ric-control-function-description.cc
                 helper/oran-interface-helper.cc
                 helper/indication-message-helper.cc
                 helper/lte-indication-message-helper.cc
                 helper/mmwave-indication-message-helper.cc
    HEADER_FILES model/oran-interface.h
                 helper/oran-interface-helper.h
                 model/asn1c-types.h
                 model/function-description.h
                 model/kpm-indication.h
                 model/kpm-function-description.h
                 model/ric-control-message.h
                 model/ric-control-function-description.h
                 helper/indication-message-helper.h
                 helper/lte-indication-message-helper.h
                 helper/mmwave-indication-message-helper.h
    LIBRARIES_TO_LINK
                     
                    ${libcore}
                    ${libmobility}
                    ${libnetwork}
                    ${ORAN_E2SIM_DIR}/e2sim/build/libe2sim.a
)

# Add oran-e2sim include directories as SYSTEM PRIVATE to both object and main library
# SYSTEM marks them as system headers (suppresses warnings), PRIVATE means they're only for this target
target_include_directories(${liboran-interface-obj} PRIVATE SYSTEM
  ${ORAN_E2SIM_DIR}/e2sim/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/e2sm_examples/kpm_e2sm/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/src/base
  ${ORAN_E2SIM_DIR}/e2sim/src/encoding
)
target_include_directories(${liboran-interface} PRIVATE SYSTEM
  ${ORAN_E2SIM_DIR}/e2sim/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/e2sm_examples/kpm_e2sm/src/ASN1c
  ${ORAN_E2SIM_DIR}/e2sim/src/base
  ${ORAN_E2SIM_DIR}/e2sim/src/encoding
)

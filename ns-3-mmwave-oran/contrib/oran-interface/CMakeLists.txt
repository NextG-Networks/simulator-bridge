include_directories(${e2sim_INCLUDE_DIRS})
include_directories(/home/hybrid/proj/oran-e2sim/e2sim/src/ASN1c)
include_directories(/home/hybrid/proj/oran-e2sim/e2sim/e2sm_examples/kpm_e2sm/src/ASN1c)
include_directories(/home/hybrid/proj/oran-e2sim/e2sim/src/base)
include_directories(/home/hybrid/proj/oran-e2sim/e2sim/src/encoding)

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
                    /home/hybrid/proj/oran-e2sim/e2sim/build/libe2sim.a
)

ocv_define_module(vcucodec opencv_core opencv_videoio WRAP python)

# Find and configure VCU/VDU/VCU2 Control Software libraries
find_library(ALLEGRO_APP_LIB allegro_app)
find_library(ALLEGRO_DECODE_LIB allegro_decode)

if (HAVE_VCU_CTRLSW OR HAVE_VCU2_CTRLSW)
    find_library(ALLEGRO_ENCODE_LIB allegro_encode)
endif()

# Find VCU/VDU/VCU2 include directory
find_path(VCU_INCLUDE_DIR
    NAMES lib_common/BufferAPI.h
    PATHS $ENV{SYSROOT}/usr/include/vcu-ctrl-sw/include
          $ENV{SYSROOT}/usr/include/vdu-ctrl-sw/include
          $ENV{SYSROOT}/usr/include/vcu2-ctrl-sw/include
    DOC "VCU/VDU/VCU2 Control Software include directory"
)

# Check if decode/encode libs were found
set(VCU_LIBS_FOUND FALSE)
if(ALLEGRO_DECODE_LIB)
    if(HAVE_VCU_CTRLSW OR HAVE_VCU2_CTRLSW)
        if(ALLEGRO_ENCODE_LIB)
            set(VCU_LIBS_FOUND TRUE)
        endif()
    else()
        set(VCU_LIBS_FOUND TRUE)
    endif()
endif()

if(ALLEGRO_APP_LIB AND VCU_INCLUDE_DIR AND VCU_LIBS_FOUND)
    message(STATUS "Control Software found:")
    message(STATUS "  ALLEGRO_APP_LIB: ${ALLEGRO_APP_LIB}")
    message(STATUS "  ALLEGRO_DECODE_LIB: ${ALLEGRO_DECODE_LIB}")
    if(HAVE_VCU_CTRLSW OR HAVE_VCU2_CTRLSW)
        message(STATUS "  ALLEGRO_ENCODE_LIB: ${ALLEGRO_ENCODE_LIB}")
    endif()
    message(STATUS "  INCLUDE_DIR: ${VCU_INCLUDE_DIR}")

    # Add VCU/VDU/VCU2 include directory to the module
    ocv_module_include_directories(${VCU_INCLUDE_DIR})

    # Link against available libraries
    set(_libs ${ALLEGRO_APP_LIB} ${ALLEGRO_DECODE_LIB})
    if(ALLEGRO_ENCODE_LIB)
        list(APPEND _libs ${ALLEGRO_ENCODE_LIB})
    endif()
    ocv_target_link_libraries(${the_module} ${_libs})

    # Add compile definition to enable VCU/VDU/VCU2 support
    if(HAVE_VCU_CTRLSW)
        target_compile_definitions(${the_module} PRIVATE HAVE_VCU_CTRLSW)
    elseif(HAVE_VDU_CTRLSW)
        target_compile_definitions(${the_mod+ule} PRIVATE HAVE_VDU_CTRLSW)
    elseif(HAVE_VCU2_CTRLSW)
        target_compile_definitions(${the_module} PRIVATE HAVE_VCU2_CTRLSW)
    endif()

    # Disable warnings as errors for vcucodec module to avoid build issues
    target_compile_options(${the_module} PRIVATE -Wno-error)

else()
    message(WARNING "VCU2 Control Software not found. VCU codec functionality will be limited.")
    message(STATUS "  HAVE_VCU_CTRLSW: ${HAVE_VCU_CTRLSW}")
    message(STATUS "  HAVE_VDU_CTRLSW: ${HAVE_VDU_CTRLSW}")
    message(STATUS "  HAVE_VCU2_CTRLSW: ${HAVE_VCU2_CTRLSW}")
    message(STATUS "  ALLEGRO_APP_LIB: ${ALLEGRO_APP_LIB}")
    message(STATUS "  ALLEGRO_DECODE_LIB: ${ALLEGRO_DECODE_LIB}")
    message(STATUS "  ALLEGRO_ENCODE_LIB: ${ALLEGRO_ENCODE_LIB}")
    message(STATUS "  VCU2_INCLUDE_DIR: ${VCU_INCLUDE_DIR}")
endif()

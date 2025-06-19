ocv_define_module(vcucodec opencv_core opencv_videoio WRAP python)

# Find and configure VCU2 Control Software libraries
find_library(ALLEGRO_APP_LIB allegro_app)
find_library(ALLEGRO_DECODE_LIB allegro_decode)
find_library(ALLEGRO_ENCODE_LIB allegro_encode)

# Find VCU2 include directory
find_path(VCU2_INCLUDE_DIR
    NAMES lib_common/BufferAPI.h
    PATHS /usr/include/vcu2-ctrl-sw/include
          $ENV{SYSROOT}/usr/include/vcu2-ctrl-sw/include
    DOC "VCU2 Control Software include directory"
)

# Check if VCU2 components were found
if(ALLEGRO_APP_LIB AND ALLEGRO_DECODE_LIB AND ALLEGRO_ENCODE_LIB AND VCU2_INCLUDE_DIR)
    message(STATUS "VCU2 Control Software found:")
    message(STATUS "  ALLEGRO_APP_LIB: ${ALLEGRO_APP_LIB}")
    message(STATUS "  ALLEGRO_DECODE_LIB: ${ALLEGRO_DECODE_LIB}")
    message(STATUS "  ALLEGRO_ENCODE_LIB: ${ALLEGRO_ENCODE_LIB}")
    message(STATUS "  VCU2_INCLUDE_DIR: ${VCU2_INCLUDE_DIR}")
    
    # Add VCU2 include directory to the module
    ocv_module_include_directories(${VCU2_INCLUDE_DIR})
    
    # Link against VCU2 libraries
    ocv_target_link_libraries(${the_module} 
        ${ALLEGRO_APP_LIB}
        ${ALLEGRO_DECODE_LIB} 
        ${ALLEGRO_ENCODE_LIB}
    )
    
    # Add compile definition to enable VCU2 support
    target_compile_definitions(${the_module} PRIVATE HAVE_VCU2_CTRLSW)
    
else()
    message(WARNING "VCU2 Control Software not found. VCU codec functionality will be limited.")
    message(STATUS "  ALLEGRO_APP_LIB: ${ALLEGRO_APP_LIB}")
    message(STATUS "  ALLEGRO_DECODE_LIB: ${ALLEGRO_DECODE_LIB}")
    message(STATUS "  ALLEGRO_ENCODE_LIB: ${ALLEGRO_ENCODE_LIB}")
    message(STATUS "  VCU2_INCLUDE_DIR: ${VCU2_INCLUDE_DIR}")
endif()

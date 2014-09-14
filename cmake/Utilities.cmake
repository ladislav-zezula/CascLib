MACRO(info)
    MESSAGE(STATUS ${ARGN})
ENDMACRO()

MACRO(warn)
    MESSAGE(WARNING ${ARGN})
ENDMACRO()

MACRO(error)
    MESSAGE(SEND_ERROR ${ARGN})
ENDMACRO()

MACRO(fatal)
    MESSAGE(FATAL_ERROR ${ARGN})
ENDMACRO()

MACRO(default_target_configuration target)
    TARGET_LINK_LIBRARIES(${target} ${LINK_LIBS})
    
    IF(${CMAKE_VERSION} VERSION_GREATER "2.8.0")
        #
        # CMake 2.8.1 is the first version with per-configuration output directories
        #
        SET_TARGET_PROPERTIES(${target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_DEBUG          ${CMAKE_CURRENT_SOURCE_DIR}/lib
            ARCHIVE_OUTPUT_DIRECTORY_RELEASE        ${CMAKE_CURRENT_SOURCE_DIR}/lib
            ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/lib
            ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL     ${CMAKE_CURRENT_SOURCE_DIR}/lib
            
            LIBRARY_OUTPUT_DIRECTORY_DEBUG          ${CMAKE_CURRENT_SOURCE_DIR}/lib
            LIBRARY_OUTPUT_DIRECTORY_RELEASE        ${CMAKE_CURRENT_SOURCE_DIR}/lib
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/lib
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL     ${CMAKE_CURRENT_SOURCE_DIR}/lib
            
            RUNTIME_OUTPUT_DIRECTORY_DEBUG          ${CMAKE_CURRENT_SOURCE_DIR}/bin
            RUNTIME_OUTPUT_DIRECTORY_RELEASE        ${CMAKE_CURRENT_SOURCE_DIR}/bin
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/bin
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     ${CMAKE_CURRENT_SOURCE_DIR}/bin
        )
    ELSE()
        set_target_properties(${target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
        )
    ENDIF()
    
    IF(WIN32 AND MSVC)

        IF(WITH_UNICODE)
            GET_TARGET_PROPERTY(pre_defs ${target} COMPILE_DEFINITIONS)
            SET_TARGET_PROPERTIES(${target} PROPERTIES COMPILE_DEFINITIONS "${pre_defs};_UNICODE;UNICODE")
        ENDIF()
        
        IF(WITH_STATIC_CRT)
            STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
            STRING(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
            
            STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
            STRING(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELEASE ${CMAKE_C_FLAGS_RELEASE})
            
            STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
            STRING(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO})
            
            STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_MINSIZEREL ${CMAKE_CXX_FLAGS_MINSIZEREL})
            STRING(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_MINSIZEREL ${CMAKE_C_FLAGS_MINSIZEREL})
        ENDIF()
    ENDIF()
    
    IF(NOT(STATIC_LIBRARY) AND APPLE)
        SET_TARGET_PROPERTIES(${target} PROPERTIES FRAMEWORK true)
        SET_TARGET_PROPERTIES(${target} PROPERTIES PUBLIC_HEADER ${PUBLIC_HEADERS})
        SET_TARGET_PROPERTIES(${target} PROPERTIES LINK_FLAGS "-framework Carbon")
    ENDIF()

    IF(NOT(STATIC_LIBRARY) AND UNIX)
        SET_TARGET_PROPERTIES(${target} PROPERTIES VERSION ${PROJECT_VERSION})
        SET_TARGET_PROPERTIES(${target} PROPERTIES SOVERSION ${PROJECT_INTERFACE_REVISION})
    ENDIF()
ENDMACRO()

MACRO(init_target_name target name)
    IF(MSVC)
        IF(WITH_STATIC_CRT)
            SET(libtype_suffix "S")
        ELSE()
            SET(libtype_suffix "D")
        ENDIF()
        
        IF(WITH_UNICODE)
            SET(charset_suffix "U")
        ELSE()
            SET(charset_suffix "A")
        ENDIF()
        
        IF(${CMAKE_VERSION} VERSION_LESS "2.8")
            SET_TARGET_PROPERTIES(${target} PROPERTIES
                DEBUG_OUTPUT_NAME          "${name}D${charset_suffix}${libtype_suffix}"
                RELEASE_OUTPUT_NAME        "${name}R${charset_suffix}${libtype_suffix}"
                RELWITHDEBINFO_OUTPUT_NAME "${name}R${charset_suffix}${libtype_suffix}"
                MINSIZEREL_OUTPUT_NAME     "${name}R${charset_suffix}${libtype_suffix}"
            )
        ELSE()
            SET_TARGET_PROPERTIES(${target} PROPERTIES
                OUTPUT_NAME_DEBUG          "${name}D${charset_suffix}${libtype_suffix}"
                OUTPUT_NAME_RELEASE        "${name}R${charset_suffix}${libtype_suffix}"
                OUTPUT_NAME_RELWITHDEBINFO "${name}R${charset_suffix}${libtype_suffix}"
                OUTPUT_NAME_MINSIZEREL     "${name}R${charset_suffix}${libtype_suffix}"
            )
        ENDIF()
    ELSE()
        SET_TARGET_PROPERTIES(${target} PROPERTIES OUTPUT_NAME ${name})
    ENDIF()
ENDMACRO()

MACRO(add_static_library target_name output_name)
    ADD_LIBRARY(${target_name} STATIC ${ARGN})
    default_target_configuration(${target_name})
    init_target_name(${target_name} ${output_name})
ENDMACRO()

MACRO(add_shared_library target_name output_name)
    ADD_LIBRARY(${target_name} SHARED ${ARGN})
    default_target_configuration(${target_name})
    init_target_name(${target_name} ${output_name})
ENDMACRO()

MACRO(add_program target_name)
    ADD_EXECUTABLE(${target_name} ${ARGN})
    default_target_configuration(${target_name})
ENDMACRO()

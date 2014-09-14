CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
INCLUDE(cmake/Utilities.cmake)

PROJECT(CascLib)
SET(PROJECT_VERSION 1.0.0)
SET(PROJECT_INTERFACE_REVISION 1)

SET(PUBLIC_HEADERS
    src/CascLib.h
    src/CascPort.h
)

IF(WIN32 AND MSVC)
    OPTION(WITH_UNICODE    "Compile with Unicode support (Windows-only)" ON)
    OPTION(WITH_STATIC_CRT "Link a static CRT library (Windows-only)"    OFF)
ENDIF()
OPTION(STATIC_LIBRARY  "Compile static linked library" OFF)
OPTION(TEST_PROGRAM    "Compile Test application"      OFF)

SET(HEADER_FILES
    ${PUBLIC_HEADERS}
    src/CascCommon.h
    src/CascMndxRoot.h
    src/common/Common.h
    src/common/FileStream.h
    src/common/ListFile.h
    src/common/Map.h
    src/jenkins/lookup.h
)

SET(SRC_FILES
    src/common/Common.cpp
    src/common/Directory.cpp
    src/common/FileStream.cpp
    src/common/ListFile.cpp
    src/common/Map.cpp
    src/jenkins/lookup3.c
    src/CascBuildCfg.cpp
    src/CascCommon.cpp
    src/CascDecompress.cpp
    src/CascDumpData.cpp
    src/CascFindFile.cpp
    src/CascMndxRoot.cpp
    src/CascOpenFile.cpp
    src/CascOpenStorage.cpp
    src/CascReadFile.cpp
)

SET(TOMCRYPT_FILES
    src/libtomcrypt/src/hashes/hash_memory.c
    src/libtomcrypt/src/hashes/md5.c
    src/libtomcrypt/src/misc/crypt_argchk.c
    src/libtomcrypt/src/misc/crypt_hash_descriptor.c
    src/libtomcrypt/src/misc/crypt_hash_is_valid.c
    src/libtomcrypt/src/misc/crypt_libc.c
)

SET(ZLIB_BZIP2_FILES
    src/zlib/adler32.c
    src/zlib/compress.c
    src/zlib/crc32.c
    src/zlib/deflate.c
    src/zlib/gzclose.c
    src/zlib/gzlib.c
    src/zlib/gzread.c
    src/zlib/gzwrite.c
    src/zlib/infback.c
    src/zlib/inffast.c
    src/zlib/inflate.c
    src/zlib/inftrees.c
    src/zlib/trees.c
    src/zlib/uncompr.c
    src/zlib/zutil.c
)

SET(TEST_SRC_FILES
    test/CascTest.cpp
)

ADD_DEFINITIONS(-D_7ZIP_ST -DBZ_STRICT_ANSI)

SET(HEADERS ${HEADER_FILES})

IF(WIN32)
    IF(MSVC)
        info("Using MSVC")
        
        # Lets generate a couple filters so that we can actually explore all
        # participating files from the Solution Explorer.
        #
        # We will start with the headers
        SOURCE_GROUP("header" FILES ${HEADER_FILES})

        # Then we sort out the actual sources
        SOURCE_GROUP("source" FILES ${SRC_FILES})
        SOURCE_GROUP("source\\zlib" FILES ${ZLIB_BZIP2_FILES})
        SOURCE_GROUP("source\\tomcrypt" FILES ${TOMCRYPT_FILES})
        
        ADD_DEFINITIONS(-DWIN32)
        IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
            ADD_DEFINITIONS(-DWIN64)
        ENDIF()
        
    ELSE()
        info("Using mingw or cygwin")
        
    ENDIF()
    
    SET(SRC_ADDITIONAL_FILES ${ZLIB_BZIP2_FILES} ${TOMCRYPT_FILES})
    SET(LINK_LIBS wininet)
    SET(CASCLIB_NAME CascLib)
    
ELSEIF(APPLE)
    info("Using Mac OS X port")
    
    SET(SRC_ADDITIONAL_FILES ${TOMCRYPT_FILES})
    SET(LINK_LIBS z bz2)
    SET(CASCLIB_NAME casc)
    
ELSEIF(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
    info("Using Linux port")
    
    OPTION(WITH_LIBTOMCRYPT "Use system LibTomCrypt library" OFF)
    IF(WITH_LIBTOMCRYPT)
        SET(LINK_LIBS z bz2 tomcrypt)
    ELSE()
        SET(LINK_LIBS z bz2)
        SET(SRC_ADDITIONAL_FILES ${TOMCRYPT_FILES})
    ENDIF()
    
    SET(CASCLIB_NAME casc)
    
ENDIF()

IF(STATIC_LIBRARY)
    add_static_library(CascLib ${CASCLIB_NAME} ${SRC_FILES} ${SRC_ADDITIONAL_FILES} ${HEADERS})
ELSE()
    add_shared_library(CascLib ${CASCLIB_NAME} ${SRC_FILES} ${SRC_ADDITIONAL_FILES} ${HEADERS})
ENDIF()

IF(TEST_PROGRAM)
    add_program(casc_test ${TEST_SRC_FILES})
    TARGET_LINK_LIBRARIES(casc_test CascLib)
ENDIF()

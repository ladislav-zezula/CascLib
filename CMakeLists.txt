cmake_minimum_required(VERSION 3.2)
project(CascLib)
include(GNUInstallDirs)

set(HEADER_FILES
    src/CascCommon.h
    src/CascLib.h
    src/CascPort.h
    src/common/Array.h
    src/common/Common.h
    src/common/Csv.h
    src/common/Directory.h
    src/common/FileStream.h
    src/common/FileTree.h
    src/common/ListFile.h
    src/common/Map.h
    src/common/Mime.h
    src/common/Path.h
    src/common/RootHandler.h
    src/common/Sockets.h
    src/jenkins/lookup.h
)

set(SRC_FILES
    src/common/Common.cpp
    src/common/Directory.cpp
    src/common/Csv.cpp
    src/common/FileStream.cpp
    src/common/FileTree.cpp
    src/common/ListFile.cpp
    src/common/Mime.cpp
    src/common/RootHandler.cpp
    src/common/Sockets.cpp
    src/jenkins/lookup3.c
    src/md5/md5.cpp
    src/CascDecompress.cpp
    src/CascDecrypt.cpp
    src/CascDumpData.cpp
    src/CascFiles.cpp
    src/CascFindFile.cpp
    src/CascIndexFiles.cpp
    src/CascOpenFile.cpp
    src/CascOpenStorage.cpp
    src/CascReadFile.cpp
    src/CascRootFile_Diablo3.cpp
    src/CascRootFile_Install.cpp
    src/CascRootFile_MNDX.cpp
    src/CascRootFile_Text.cpp
    src/CascRootFile_TVFS.cpp
    src/CascRootFile_OW.cpp
    src/CascRootFile_WoW.cpp
)

set(LINK_LIBS)
find_package(ZLIB)
if (ZLIB_FOUND)
    set(LINK_LIBS ${LINK_LIBS} ZLIB::ZLIB)
else()
    set(SRC_FILES ${SRC_FILES}
        src/zlib/adler32.c
        src/zlib/crc32.c
        src/zlib/inffast.c
        src/zlib/inflate.c
        src/zlib/inftrees.c
        src/zlib/zutil.c
    )
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/zlib)
endif()

set(TEST_SRC_FILES
    test/CascTest.cpp
)

add_definitions(-D_7ZIP_ST -DBZ_STRICT_ANSI)

option(CASC_UNICODE "Compile UNICODE version instead of ANSI one (Visual Studio only)" OFF)

if(WIN32)
    if(MSVC)
        message(STATUS "Using MSVC")
        add_definitions(-D_7ZIP_ST)
    else()
        message(STATUS "Using mingw")
    endif()
    set(LINK_LIBS ${LINK_LIBS} wininet)
    if(CASC_UNICODE)
    message(STATUS "Build UNICODE version")
        add_definitions(-DUNICODE -D_UNICODE)
    else()
    message(STATUS "Build ANSI version")
    endif()
endif()

option(CASC_BUILD_SHARED_LIB "Compile dynamically linked library" ON)
if(CASC_BUILD_SHARED_LIB)
    message(STATUS "Build dynamically linked library")
    add_library(casc SHARED ${SRC_FILES} ${HEADER_FILES})

    if(APPLE)
        set_target_properties(casc PROPERTIES FRAMEWORK true)
        set_target_properties(casc PROPERTIES PUBLIC_HEADER "src/CascLib.h src/CascPort.h")
        set_target_properties(casc PROPERTIES LINK_FLAGS "-framework Carbon")
    endif()

    if(UNIX)
        set_target_properties(casc PROPERTIES VERSION 1.0.0)
        set_target_properties(casc PROPERTIES SOVERSION 1)
    endif()

    target_link_libraries(casc ${LINK_LIBS})
    install(TARGETS casc
        EXPORT CascLibTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FRAMEWORK DESTINATION /Library/Frameworks)
    target_include_directories(casc
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    # On Win32, build CascLib.dll
    if(WIN32)
        set_target_properties(casc PROPERTIES OUTPUT_NAME CascLib)
    endif()
endif()

option(CASC_BUILD_TESTS "Build Test application" OFF)
if(CASC_BUILD_TESTS)
    set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "Force Static library building to link test app" FORCE)
    message(STATUS "Build Test application")
    add_executable(casc_test ${TEST_SRC_FILES})
    set_target_properties(casc_test PROPERTIES LINK_FLAGS "-pthread")
    target_link_libraries(casc_test casc_static)
    install(TARGETS casc_test RUNTIME DESTINATION bin)
endif()

option(CASC_BUILD_STATIC_LIB "Build static linked library" OFF)
if(CASC_BUILD_STATIC_LIB)
    message(STATUS "Build static linked library")
    add_library(casc_static STATIC ${SRC_FILES} ${HEADER_FILES})
    target_link_libraries(casc_static ${LINK_LIBS})
    set_target_properties(casc_static PROPERTIES OUTPUT_NAME casc)
    target_include_directories(casc_static
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    install(TARGETS casc_static
        EXPORT CascLibTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FRAMEWORK DESTINATION /Library/Frameworks)

    if(APPLE)
        set_target_properties(casc_static PROPERTIES FRAMEWORK false)
        set_target_properties(casc_static PROPERTIES PUBLIC_HEADER "src/CascLib.h src/CascPort.h")
        set_target_properties(casc_static PROPERTIES LINK_FLAGS "-framework Carbon")
    endif()

    if(UNIX)
        set_target_properties(casc_static PROPERTIES VERSION 1.0.0)
        set_target_properties(casc_static PROPERTIES SOVERSION 1)
    endif()
endif()


install(FILES src/CascLib.h src/CascPort.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(VERSION_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/CascLibConfigVersion.cmake")
set(PROJECT_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/CascLibConfig.cmake")
set(INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/CascLib")

include(CMakePackageConfigHelpers)

write_basic_package_version_file("${VERSION_CONFIG}" VERSION 2.1 COMPATIBILITY SameMajorVersion)
configure_package_config_file("Config.cmake.in"
                              "${PROJECT_CONFIG}"
                              INSTALL_DESTINATION "${INSTALL_DIR}")

install(FILES "${PROJECT_CONFIG}" "${VERSION_CONFIG}" DESTINATION "${INSTALL_DIR}")
install(EXPORT CascLibTargets NAMESPACE CascLib:: DESTINATION "${INSTALL_DIR}")

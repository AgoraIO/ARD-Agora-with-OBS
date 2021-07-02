cmake_minimum_required(VERSION 3.5)
project(agora-tool-ui VERSION 1.0.0)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
	include(external/FindLibObs.cmake)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(AGORA_ARCH "x64")
	else()
		set(AGORA_ARCH "x86")
	endif()
endif()

find_package(LibObs REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

if(MSVC)
	set(win-capture_PLATFORM_DEPS
		w32-pthreads)
endif()

set(agora-tool-ui_SOURCES
	${agora-tool-ui_SOURCES}
	src/agora-ui-main.cpp
	src/forms/window-agora-main.cpp
	src/forms/window-agora-settings.cpp
	src/Agora/agorartcengine.cpp
	src/agora-plugin/agora-output.cpp
    src/agora-plugin/agora-pcm-encoder.cpp)

set(agora-tool-ui_HEADERS
	${agora-tool-ui_HEADERS}
	src/agora-ui-main.h
	src/forms/window-agora-main.hpp
	src/forms/window-agora-settings.hpp
	src/Agora/agorartcengine.hpp)

add_library(agora-tool-ui MODULE
		${Qt5Core_INCLUDES}
		${Qt5Widgets_INCLUDES}
		${LIBOBS_INCLUDE_DIR}
		"${CMAKE_SOURCE_DIR}/deps/libyuv/include"
		${agora-tool-ui_SOURCES}
		${agora-tool-ui_HEADERS}
		)

if (WIN32)
	include_directories(
			"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
			${Qt5Core_INCLUDES}
			${Qt5Widgets_INCLUDES}
			"${CMAKE_SOURCE_DIR}/deps/Agora/include"
			"${CMAKE_SOURCE_DIR}/deps/libyuv/include"
			"${CMAKE_SOURCE_DIR}/deps/curl/include")

endif ()
if(APPLE)
	include_directories(
			"${CMAKE_SOURCE_DIR}/../UI/obs-frontend-api"
			"${CMAKE_SOURCE_DIR}/deps/mac/include"
			"${CMAKE_SOURCE_DIR}/deps/mac/include/QtCore"
			"${CMAKE_SOURCE_DIR}/deps/mac/include/QtGui"
			"${CMAKE_SOURCE_DIR}/deps/mac/include/QtWidgets"
			"${CMAKE_SOURCE_DIR}/deps/libyuv/include")

	find_library(AGORAAIDENOISEEXTENSION AgoraAIDenoiseExtension "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AGORACORE AgoraCore "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AGORAFDKAAC Agorafdkaac "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AGORAFFMPEG Agoraffmpeg "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AGORARTCKIT AgoraRtcKit "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AGORASOUNDTOUCH AgoraSoundTouch "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(AV1 av1 "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(YUV yuv "${CMAKE_SOURCE_DIR}/deps/mac/libs")
	find_library(obs-frontend-api obs-frontend-api "${CMAKE_SOURCE_DIR}/deps/mac/libs")

	find_library(QT5CORE QtCore "${CMAKE_SOURCE_DIR}/deps/mac/lib")
	find_library(QT5GUI QtGui "${CMAKE_SOURCE_DIR}/deps/mac/lib")
	find_library(QT5WIDGETS QtWidgets "${CMAKE_SOURCE_DIR}/deps/mac/lib")
  find_library(CURL curl "${CMAKE_SOURCE_DIR}/deps/mac/libs")




endif()

if(WIN32)
	target_link_libraries(agora-tool-ui
		libobs
		Qt5::Core
		Qt5::Widgets
		"${CMAKE_SOURCE_DIR}/deps/Agora/${AGORA_ARCH}/agora_rtc_sdk.lib"
		"${CMAKE_SOURCE_DIR}/deps/libyuv/${AGORA_ARCH}/yuv.lib"
		"${CMAKE_SOURCE_DIR}/deps/curl/${AGORA_ARCH}/libcurl.lib")
endif()

if(APPLE)
	target_link_libraries(agora-tool-ui
			libobs
			${AGORAAIDENOISEEXTENSION}
			${AGORACORE}
			${AGORAFDKAAC}
			${AGORAFFMPEG}
			${AGORARTCKIT}
			${AGORASOUNDTOUCH}
			${AV1}
			${YUV}
			${obs-frontend-api}
			${QT5CORE}
			${QT5GUI}
			${QT5WIDGETS}
      ${CURL}
			)
endif()


if(WIN32)
	if(NOT DEFINED OBS_FRONTEND_LIB)
		set(OBS_FRONTEND_LIB "OBS_FRONTEND_LIB-NOTFOUND" CACHE FILEPATH "OBS frontend library")
		message(FATAL_ERROR "Could not find OBS Frontend API's library !")
	endif()

    if(MSVC)
        # Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL)
	    add_definitions(/MP /d2FH4-)
    endif()
	
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(ARCH_NAME "64bit")
		set(OBS_BUILDDIR_ARCH "build64")
	else()
		set(ARCH_NAME "32bit")
		set(OBS_BUILDDIR_ARCH "build32")
	endif()

	include_directories(
		"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/UI"
	)
	
	target_link_libraries(agora-tool-ui
		${OBS_FRONTEND_LIB}/Release/obs-frontend-api.lib)

	set(RELEASE_DIR "${PROJECT_SOURCE_DIR}/release")

	add_custom_command(TARGET agora-tool-ui POST_BUILD
		# If config is Release, package release files
		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E make_directory
			"${RELEASE_DIR}/data/obs-plugins/agora-tool-ui"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}"
			"${RELEASE_DIR}/bin/${ARCH_NAME}")

		COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy_directory
			"${PROJECT_SOURCE_DIR}/data"
			"${RELEASE_DIR}/data/obs-plugins/agora-tool-ui")

		COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy
			"$<TARGET_FILE:agora-tool-ui>"
			"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

			
		# Copy to obs-studio dev environment for immediate testing
		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy
				"$<TARGET_FILE:agora-tool-ui>"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy
				"$<TARGET_PDB_FILE:agora-tool-ui>"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E make_directory
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/data/obs-plugins/agora-tool-ui")

		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/data"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/data/obs-plugins/agora-tool-ui")

		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E copy
				"$<TARGET_FILE:agora-tool-ui>"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		#COMMAND if $<CONFIG:Release>==1 (
		#	"${CMAKE_COMMAND}" -E copy
		#		"$<TARGET_PDB_FILE:agora-tool-ui>"
		#		"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E make_directory
			"${LIBOBS_LIB}/../rundir/$<CONFIG>/data/obs-plugins/agora-tool-ui")

		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/data"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/data/obs-plugins/agora-tool-ui")

		# Copy to agora dev environment for immediate testing
		COMMAND if $<CONFIG:Debug>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/deps/Agora/${AGORA_ARCH}/"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/bin/${ARCH_NAME}/")

		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/deps/Agora/${AGORA_ARCH}/"
				"${LIBOBS_LIB}/../rundir/$<CONFIG>/bin/${ARCH_NAME}/")
	  
		COMMAND if $<CONFIG:Release>==1 (
			"${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/deps/Agora/${AGORA_ARCH}/"
				"${RELEASE_DIR}/bin/${ARCH_NAME}/")
	)
endif()

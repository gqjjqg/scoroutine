INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/inc)

SET(SRC_LIST main.c)

SET(LIB_LIST "coroutine")

#头文件
FILE(GLOB_RECURSE MODULE_SRC_LIST "../inc/*" "*.h")
FOREACH(src ${MODULE_SRC_LIST})
STRING(REGEX REPLACE ".*/(inc/.*/|inc/).*" ${PROJECT_SOURCE_DIR}/\\1 dst ${src} )
	SET(equal FALSE)
	FOREACH(temp ${MODULE_HEAD_LIST})
		STRING(COMPARE EQUAL ${dst} ${temp} equal)
		IF (equal)
			BREAK()
		ENDIF (equal)
	ENDFOREACH(temp ${MODULE_HEAD_LIST})
	
	IF (NOT equal)
		SET(MODULE_HEAD_LIST ${MODULE_HEAD_LIST} ${dst})
		INCLUDE_DIRECTORIES(${dst})
	ENDIF (NOT equal)
	
ENDFOREACH()

#库文件
FOREACH(library ${LIB_LIST})
	MESSAGE( "find ${library} :" )
	
	SET(var "NOTFOUND")
	
	FIND_LIBRARY(var ${library} ${PROJECT_SOURCE_DIR}/../bin 
		${PROJECT_SOURCE_DIR}/../bin/Debug 
		${PROJECT_SOURCE_DIR}/../bin/Release)
	
	IF(NOT var) #var STREQUAL "NOTFOUND"
		MESSAGE(FATAL_ERROR "${library} not found")
	ENDIF()

	MESSAGE( ${var} )
	SET(ALL_LIBRARY ${ALL_LIBRARY} ${var})
	
ENDFOREACH(library)	

ADD_EXECUTABLE(libcoroutine-test ${SRC_LIST})
	
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../bin)

TARGET_LINK_LIBRARIES(libcoroutine-test ${ALL_LIBRARY})

IF (${MSVC})
FILE(COPY ${PROJECT_SOURCE_DIR}/test
DESTINATION  "${CMAKE_BINARY_DIR}/test" 
)
ENDIF (${MSVC})
INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/src")

ADD_EXECUTABLE(trfb_test trfbtest.c)
TARGET_LINK_LIBRARIES(trfb_test trfb pthread)


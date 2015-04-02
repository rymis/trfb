INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/src")
INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/libwebcam")

ADD_EXECUTABLE(trfb_test trfbtest.c)
TARGET_LINK_LIBRARIES(trfb_test trfb pthread)

ADD_EXECUTABLE(webcam_jpeg test_jpeg.c)
TARGET_LINK_LIBRARIES(webcam_jpeg jpeg webcam v4l2)


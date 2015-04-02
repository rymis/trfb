INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/src")

ADD_EXECUTABLE(rfbcam webcam/webcam.c rfbcam.c)
TARGET_LINK_LIBRARIES(rfbcam trfb pthread v4l2)


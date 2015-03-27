INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(trfb_test server.c trfb.c error.c connection.c trfbtest.c tinycthread.c)

TARGET_LINK_LIBRARIES(trfb_test -lpthread)


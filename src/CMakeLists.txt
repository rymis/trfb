INCLUDE_DIRECTORIES(.)

ADD_LIBRARY(trfb server.c trfb.c error.c connection.c protocol.c io.c fb.c tinycthread.c)



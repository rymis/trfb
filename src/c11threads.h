#ifndef C11_THREADS_INC
#define C11_THREADS_INC

#ifdef HAVE_THREADS_H
# include <threads.h>
#else
# include "tinycthread.h"
#endif

#endif



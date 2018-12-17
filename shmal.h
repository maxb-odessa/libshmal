
#ifndef _SHMAL_H_INCLUDED_
#define _SHMAL_H_INCLUDED_

#include "config.h"

#if !defined(USE_SYSVIPC) && !defined(USE_MMAP)
 #error please define one of USE_SYSVIPC or USE_MMAP
#endif

#if defined(USE_SYSVIPC) && defined(USE_MMAP)
 #error USE_SYSVIPC and USE_MMAP may not be defined together
#endif

#warning TODO stupid macros' above...

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(USE_SYSVIPC)
 #include <sys/shm.h>
#elif defined(USE_MMAP)
 #include <sys/mman.h>
#endif

#ifndef __USE_UNIX98   // for pthread_mutexattr_settype()
 #define __USE_UNIX98
 #include <pthread.h>
 #undef __USE_UNIX98
#else
 #include <pthread.h>
#endif

//! cell data structure
typedef struct {
  off_t    offset;      //!< memory start offset (addr of cells_pool + offset)
  unsigned cells:31;    //!< number of free/busy "cell_size" blocks after this one (and including it)
  unsigned free :1;     //!< free or busy cell(s)
} cell_data_t;

#warning TODO why we hardcoded that 31 bits? let's user deside which
#warning TODO model to choose. Or... not?
#warning TODO and why use "off_t"? they could be 64 bit and we don't need that
#warning TODO large and mem-vasty data type

//! stats struct for shm_info_t
//! and is common for every process that uses it
typedef struct {
  unsigned cells_taken;      //!< cells currently in use
  long     free_calls;       //!< free() calls counter
  long     alloc_calls;      //!< alloc() calls counter
  long     free_fails;       //!< free() calls failures
  long     alloc_fails;      //!< alloc() calls failures
} shmal_stats_t;

#warning TODO that stats collection is useful in debug only

//! this struct is placed in shmem (no ptr, just offsets)
//! and is common for every process that uses it
typedef struct {
  pthread_mutex_t mutex;              //!< shmem segment mutex 
  off_t           cells_data_offset;  //!< offset from shmem start
  off_t           cells_pool_offset;  //!< offset from shmem start
  unsigned        cell_size;          //!< this segment info data: cell size
  unsigned        cells_num;          //!< cells num
  shmal_stats_t   stats;              //!< statistics
} shmal_info_t;

//! this struct is in local addrspace, no offsets, just adjusted addresses
typedef struct {
#if defined(USE_MMAP)
  char         *fname;          //!< filename for mmap()
#elif defined(USE_SYSVIPC)
  key_t        key;             //!< shmem key for shmget()
#endif
  int          id;              //!< file id for mmap() or an id for shmget()
  unsigned     cells_num;       //!< number of cells
  unsigned     cell_size;       //!< size of cell, MUST BE POWER OF 2 !! (16, 32, 64, ...)
  size_t       memsize;         //!< actual allocated mem segment size
  cell_data_t  *cells_data;     //!< local addr!
  void         *cells_pool;     //!< local addr!
  shmal_info_t *shm_info;       //!< actual shmem info (start block)
} shmal_ctx_t;


#define OFF_TO_ADDR(shm, off)     ((void *)((char *)(shm)->cells_pool + (off_t)(off)))
#define ADDR_TO_OFF(shm, addr)    ((off_t)((char *)(addr) - (char *)(shm)->cells_pool))

#warning TODO I don't like those macros' names...

#define SHMAL_LOCK(shm)       pthread_mutex_lock(((shm)->shm_info->mutex))
#define SHMAL_UNLOCK(shm)     pthread_mutex_unlock(((shm)->shm_info->mutex))

//! offset value to represent (void*)NULL
#define NULLOFF  (-1)

//! limits
#define MAX_CELLS_NUM     0x80000000     //!< 2^31 cells
#define MIN_CELLS_NUM     1              //!< 1 cell
#define MAX_CELL_SIZE     0x100000       //!< 1 meg single cell size
#define MIN_CELL_SIZE     16             //!< 16 bytes single cell size

#warning TODO aha, "off_t" should not be longer then 32 bits and on
#warning TODO x64 platform it is... 64 bits, so let's make it like u_int_32

#warning TODO great thoughts are filling my mind: should we stick to
#warning TODO hardcoded 32 bits offsets or rely on sizeof(int) ?

extern int shmal_attach(shmal_ctx_t *);
extern int shmal_create(shmal_ctx_t *);
extern int shmal_destroy(shmal_ctx_t *);
extern int shmal_detach(shmal_ctx_t *);
extern off_t shmal_alloc_off(shmal_ctx_t *, size_t, off_t);
extern int shmal_free(shmal_ctx_t *, off_t);
extern off_t shmal_strdup(shmal_ctx_t *, char *);
extern int shmal_clear(shmal_ctx_t *);

#define shmal_alloc(shm, size)          shmal_alloc_off((shm), (size), -1)

#endif //_SHMAL_H_INCLUDED_


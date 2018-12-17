
#include "shmal.h"

/*!
 * \brief check boundaries or requested total mem size and calc real needed size
 * \param shm - shmem segment context pointer
 * \return real requested shmem size
 */
static size_t shmal_real_mem_size(shmal_ctx_t *shm)
{

  // check limits
  if (shm->cell_size > MAX_CELL_SIZE ||
      shm->cell_size < MIN_CELL_SIZE ||
      shm->cells_num > MAX_CELLS_NUM ||
      shm->cells_num < MIN_CELLS_NUM ||
      shm->cell_size & (shm->cell_size - 1)) // cell_size must be power of 2 (why?)
#warning TODO really, why?
  { 
    errno = EINVAL;
    return 0;
  }

  // calc the size and return
  return sizeof(shmal_info_t) + sizeof(cell_data_t) * shm->cells_num + shm->cells_num * shm->cell_size;
}


/*!
 * \brief get shmem core: use shmget() or mmap() to get shared mem chunk
 * \param shm - shmem segment context pointer
 * \return shmem segment addr or NULL if failed
 */
#warning TODO POSIX IPC support
#warning TODO type of shmem as an argument, not compiletime
static void *shmal_get_core(shmal_ctx_t *shm)
{
  void *p;
#if defined(USE_SYSVIPC)
  struct shmid_ds shmst;
#endif

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): requested %d bytes\n", __FILE__, __LINE__, __FUNCTION__, shm->memsize);
#endif

  // request for shmem segment
#if defined(USE_SYSVIPC)
  shm->id = shmget(shm->key, shm->memsize, IPC_CREAT|IPC_EXCL|S_IRWXU);
#elif defined (USE_MMAP)
  if (shm->fname == NULL) {
    errno = EINVAL;
    return NULL;
  }
  // open/create/truncate the file
  shm->id = open(shm->fname, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);
#endif

  // shmget or file open failed...
  if (shm->id < 0) {
    int rc = errno;
    errno = rc;
    return NULL;
  }

#if defined(USE_MMAP)
  // extend file to requested size
  if (lseek(shm->id, shm->memsize, SEEK_SET) < 0) {
    int rc = errno;
    close(shm->id);
    unlink(shm->fname);
    errno = rc;
    return NULL;
  }
  
  // write something to the end to make file actually expand  
  if (write(shm->id, "", 1) < 1) {
    int rc = errno;
    close(shm->id);
    unlink(shm->fname);
    errno = rc;
    return NULL;
  }
#endif

#if defined(USE_SYSVIPC)
  // attach to shmem segment
  p = shmat(shm->id, NULL, 0);
#elif defined(USE_MMAP)
  // mmap file
  p = mmap(NULL, shm->memsize, PROT_READ|PROT_WRITE, MAP_SHARED, shm->id, 0);
#endif

  // shmat or mmap failed...
  if (p == (void*)-1) {
    int rc = errno;
#if defined(USE_SYSVIPC)
    shmctl(shm->id, IPC_RMID, 0);
#elif defined(USE_MMAP)
    close(shm->id);
    unlink(shm->fname);
#endif
    errno = rc;
    return NULL;
  }

#if defined(USE_SYSVIPC)
  // get actual allocated shmem segment size
  if (!shmctl(shm->id, IPC_STAT, &shmst) >= 0)
    shm->memsize = shmst.shm_segsz;
#endif

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): allocated %d bytes\n", __FILE__, __LINE__, __FUNCTION__, shm->memsize);
#endif

  // zero the segment
  bzero(p, shm->memsize);

  return p;
}

/*!
 * \brief attach to existing shmem segment and adjust local addresses
 * \param shm - shmem segment context pointer
 * \return -1 if error, 0 if ok
 */
int shmal_attach(shmal_ctx_t *shm)
{
  // check shmem params and calculate requested shmem size
  shm->memsize = shmal_real_mem_size(shm);
  if (shm->memsize == 0)
    return -1;

#if defined(USE_SYSVIPC)
  // try to attach to shmem
  shm->id = shmget(shm->key, shm->memsize, S_IRWXU);
  if (shm->id < 0)
    return -1;
  else
    shm->shm_info = (shmal_info_t *)shmat(shm->id, NULL, 0);
#elif defined(USE_MMAP)
  // open the file
  shm->id = open(shm->fname, O_RDWR, S_IRWXU);
  if (shm->id < 0)
    return -1;
  // mmap the file
  shm->shm_info = (shmal_info_t *)mmap(NULL, shm->memsize, PROT_READ|PROT_WRITE, MAP_SHARED, shm->id, 0);
#endif

  if ((void *)shm->shm_info == (void*)-1)
    return -1;

  // check! (not our segment?)
  if (shm->shm_info->cell_size != shm->cell_size || shm->shm_info->cells_num != shm->cells_num) {
    shmal_detach(shm);
    return -1;
  }

  // cells data starts right after info structure
  shm->cells_data = (cell_data_t *)((char *)shm->shm_info + shm->shm_info->cells_data_offset);

  // data pool starts right after last cell data holder
  shm->cells_pool = (void *)((char *)shm->shm_info + shm->shm_info->cells_pool_offset);

  return 0;
}



/*!
 * \brief init shmem - calc required size, allocate segment and init its context
 * \param shm - shmem segment context pointer
 * \return 0 if ok, -1 otherwise
 */
int shmal_create(shmal_ctx_t *shm)
{
  int i;
  pthread_mutexattr_t ma;

  // check shmem params and calculate requested shmem size
  shm->memsize = shmal_real_mem_size(shm);
  if (shm->memsize == 0)
    return -1;

  // allocate mem for total of cells_data + cells_pool
  shm->shm_info = (shmal_info_t *)shmal_get_core(shm);
  if (!shm->shm_info) {
    errno = ENOMEM;
    return -1;
  }

  // cells data starts right after info structure
  shm->cells_data = (cell_data_t *)(shm->shm_info + 1);

  // save offset in shmem info block too
  shm->shm_info->cells_data_offset = (char *)shm->cells_data - (char *)shm->shm_info;

  // data pool starts right after last cell data holder
  shm->cells_pool = (void *)(shm->cells_data + shm->cells_num);

  // save offset in shmem info block too
  shm->shm_info->cells_pool_offset = (char *)shm->cells_pool - (char *)shm->shm_info;

  // preserve cell size and their amount in shmem
  shm->shm_info->cell_size = shm->cell_size;
  shm->shm_info->cells_num = shm->cells_num;

  // init shmem mutex and make it shared
  pthread_mutexattr_init(&ma);
  pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_NORMAL);
  pthread_mutex_init(&(shm->shm_info->mutex), &ma);

  // init cells
  for (i = 0; i < shm->cells_num; i ++) {
    shm->cells_data[i].offset = i * shm->cell_size;  // offset from cells_pool
    shm->cells_data[i].cells = shm->cells_num - i;   // num of free cells after this (including it)
    shm->cells_data[i].free = 1;                     // free cell
  }

  // all ok
  errno = 0;
  return 0;
}

/*!
 * \brief detach from the segment
 * \param shm - shmem segment context pointer
 * \return 0 if ok, -1 otherwise
 */
int shmal_detach(shmal_ctx_t *shm)
{
  // detach from the segment
  if (shm->shm_info) {
#if defined(USE_SYSVIPC)
    shmdt((void*)shm->shm_info);
#elif defined(USE_MMAP)
    msync(shm->shm_info, shm->memsize, MS_ASYNC);
    munmap(shm->shm_info, shm->memsize);
    close(shm->id);
#endif
  } else {
    errno = EINVAL;
    return -1;
  }

  return 0;
}


/*!
 * \brief detach and destroy attached shmem segment and all its data
 * \param shm - shmem segment context pointer
 * \return -1 if error, 0 otherwise
 */
#warning TODO add an argument that should indicate if we must
#warning TODO hard-destroy the segment or do nothing if someone
#warning TODO still uses it
#warning TODO also do not detach automatically from it, let user call
#warning TODO shmal_detach() itself
int shmal_destroy(shmal_ctx_t *shm)
{
  int rc;

#if defined(USE_SYSVIPC)
  // mark as destroyed
  rc = shmctl(shm->id, IPC_RMID, 0);
#endif

  // detach from shmem
  if (shmal_detach(shm) != 0)
    return -1;

#if defined(USE_MMAP)
  rc = unlink(shm->fname);
#endif

  // clear shm data fo safety
  if (rc == 0)
    bzero(shm, sizeof(*shm));

  return rc;
}

/*!
 * \brief allocate mem chunk in segment staring from given offset
 * \param shm - shmem segment context pointer
 * \param size - requested size
 * \param start_off - mem chunk start offset
 * \return offset or NULLOFF on error
 */
off_t shmal_alloc_off(shmal_ctx_t *shm, size_t size, off_t start_off)
{
  register int i, j;
  int free_cell = -1, ncells;
  off_t off;

  // stats
  shm->shm_info->stats.alloc_calls ++;

  // sanity checks
  if (!shm || size <= 0 || size > shm->cell_size * shm->cells_num) {
    shm->shm_info->stats.alloc_fails ++;
    errno = EINVAL;
    return NULLOFF;
  }

  // how much cells we might need?
  ncells = size / shm->cell_size;
  if (ncells == 0 || size % shm->cell_size)
    ncells ++;

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): requested %d bytes (%d blocks)\n", __FILE__, __LINE__, __FUNCTION__, size, ncells);
#endif

  // too much blocks requested :(
  if (ncells > shm->cells_num) {
#ifdef DEBUG
    fprintf(stderr, "%s:%d %s(): error - requested chunk is too big\n", __FILE__, __LINE__, __FUNCTION__);
#endif
    shm->shm_info->stats.alloc_fails ++;
    errno = ENOMEM;
    return NULLOFF;
  }

  // start_off is >= 0, so try to allocate space at given offset
  if (start_off >= 0) {
 
    // check for cells chunk validness and save the offset
    if (shm->cells_data[start_off].free && shm->cells_data[start_off].cells >= ncells) {
      free_cell = start_off;
      // walk back and adjust "cells" values for free cells that might come
      // before us (this is not required in case of automatic free chunk search)
      for (i = start_off - 1, j = 1; i >= 0 && shm->cells_data[i].free; i --, j ++)
        shm->cells_data[i].cells = j;
    } else
      errno = EBUSY;
   
  } else {

    // start_off is negative, find free cells ourself:
    // find free cell with exactly "ncells" free chunk
    // or first big enough chunk
    for (errno = ENOMEM, free_cell = -1, i = 0; i < shm->cells_num; i += shm->cells_data[i].cells) {
      // found free big enough block?
      if (shm->cells_data[i].free && shm->cells_data[i].cells >= ncells) {
        free_cell = i;
        break;
      }
    }

  } //if(..)

  // no more mem or allocation at start_off failed
  if (free_cell < 0) {
#ifdef DEBUG
    fprintf(stderr, "%s:%d %s(): no free mem\n", __FILE__, __LINE__, __FUNCTION__);
#endif
    shm->shm_info->stats.alloc_fails ++;
    return NULLOFF;
  }

  // now "free_cell" is an our free chunk start index

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): allocated %d cells at offset %d\n", 
         __FILE__, __LINE__, __FUNCTION__,
         free_cell, 
         shm->cells_data[free_cell].cells, (int)shm->cells_data[free_cell].offset);
#endif

  // found, adjust its data and return pointer
  // also update all subsequent cells
  off = shm->cells_data[free_cell].offset;

  // update taken cells
  for (j = ncells; j > 0; free_cell ++, j --) {
    shm->cells_data[free_cell].free = 0;
    shm->cells_data[free_cell].cells = j;
  }

  errno = 0;

  // stats
  shm->shm_info->stats.cells_taken += ncells;

  // zero mem chunk
  bzero((void*)OFF_TO_ADDR(shm, off), size);

  // return pointer
  return off;
}


/*!
 * \brief free shmem chunk - find taken cells by offset and free all the chunk
 * \param shm - shmem segment context pointer
 * \param off - allocated mem chunk start offset
 * \return 0 if ok, NULLOFF otherwise
 */
int shmal_free(shmal_ctx_t *shm, off_t off)
{
  unsigned i, free_cells, first, last, our_cell, saved_cells;

  // sanity checks
  if (!shm || off < 0 || off > shm->cells_num * shm->cell_size) {
    shm->shm_info->stats.free_fails ++;
    errno = EINVAL;
    return NULLOFF;
  }

  // stats
  shm->shm_info->stats.free_calls ++;

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): request for offset %d", __FILE__, __LINE__, __FUNCTION__, (int)off);
#endif

  // search for needed starting cell by its offset
  // (calc its index actually)

  // offset is not aligned to cell_size? error...
  if (off % shm->cell_size) {
#ifdef DEBUG
    fprintf(stderr, "%s:%d %s(): invalid offset %d\n", __FILE__, __LINE__, __FUNCTION__, (int)off);
#endif
    errno = EINVAL;
    shm->shm_info->stats.free_fails ++;
    return NULLOFF;
  } else
    our_cell = off / shm->cell_size;

  // small safety check
  if (shm->cells_data[our_cell].free) {
#ifdef DEBUG
    fprintf(stderr, "%s:%d %s(): offset %d is invalid (already free cell)\n", __FILE__, __LINE__, __FUNCTION__, (int)off);
#endif
    errno = EINVAL;
    shm->shm_info->stats.free_fails ++;
    return NULLOFF;
  }

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): found cell #%d\n", __FILE__, __LINE__, __FUNCTION__, our_cell);
#endif

  // preserve taken cells num for statistics
  saved_cells = shm->cells_data[our_cell].cells;
 
  // see if there are free cells before and after
  // this chunk and adjust their "cells" values

  // look back and count possible free cells

  // our cell is not first in list
  if (our_cell > 0) {

    // walk back to first non-free cell
#warning TODO this is slow, try to calculate the value or optimize
#warning TODO the search
    for (i = our_cell - 1; i && shm->cells_data[i].free; i --);

    // 'i' is in the middle so we just hit non-free cell
    first = i;
#warning TODO what will happen in "i=0" case?
    if (!shm->cells_data[i].free)
      first ++; // "i" points at non-free cell, advance to next that is free

  } else
    first = our_cell;  // our cell is the first

  // look forth and count next cells to free
  last = our_cell + shm->cells_data[our_cell].cells;

  // calc total free cells by their index difference
  free_cells = last - first;

#ifdef DEBUG
  fprintf(stderr, "%s:%d %s(): first free cell #%d, last free cell #%d (total %d cells)\n", 
          __FILE__, __LINE__, __FUNCTION__,
          first, last, free_cells);
#endif

  // mark all of them as free and adjust "cells" value
  for (i = first; i < last; i ++, free_cells --) {
     shm->cells_data[i].free = 1;
     shm->cells_data[i].cells = free_cells;
  } 

  // statistics
  shm->shm_info->stats.cells_taken -= saved_cells;

  // all done
  errno = 0;
  return 0;
}

/*!
 * \brief clear shmem - free all cells and reinit them with zero
 * \param shm - shmem segment context pointer
 * \return 0 if ok, -1 otherwise
 */
int shmal_clear(shmal_ctx_t *shm)
{
  int i;

  // sanity check
  if (!shm) {
    errno = EINVAL;
    return -1;
  }

  // walk through cells from end to start and reinit them
  for (i = 0; i < shm->cells_num; i ++) {
     shm->cells_data[i].free = 1;
     shm->cells_data[i].cells = shm->cells_num - i;
  }

  return 0;
}


/*!
 * \brief make a copy of regular string into shmem
 * \param shm - shmem segment context pointer
 * \param ptr - pointer at local string
 * \return valid offset or NULLOFF if copying failed
 */
off_t shmal_strdup(shmal_ctx_t *shm, char *ptr)
{
  off_t off;

  // sanity checs
  if (!ptr || !shm) {
    errno = EINVAL;
    return NULLOFF;
  }

  // allocate shmem chunk for the string
  off = shmal_alloc(shm, strlen(ptr) + 1);

  // copy if success
  if (off != NULLOFF)
    strcpy(OFF_TO_ADDR(shm, off), ptr);

  return off;
}




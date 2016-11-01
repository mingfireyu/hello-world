#ifndef __DEFINE_HH__
#define __DEFINE_HH__

#include <set>
#include <vector>
#include <utility>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <boost/thread/shared_mutex.hpp>
#include "ds/hotness.hh"
#include "enum.hh"
#include <string.h> //strerror
// use block-based interface
#define BLOCK_ITF

// diskmod
#define DISK_DIRECT_IO  
#define DISK_BLKSIZE    (512)
#define DIRECT_LBA_CONTAINER_MAPPING    1

/** align the buffer with block size in memory for direct I/O **/
static inline void* buf_malloc (unsigned int s) {
#ifdef DISK_DIRECT_IO
    void *buf = NULL;
    int ret = posix_memalign(&buf,DISK_BLKSIZE,s);
    if(!ret){
      strerror(ret);
    }
    //    return memalign(DISK_BLKSIZE, s);
    return buf;
#else
    return malloc(s);
#endif
}

static inline void* buf_calloc (unsigned int s, unsigned int unit) {
    void* ret = buf_malloc(s * unit);
    if (ret != nullptr) memset(ret, 0, s * unit);
    return ret;
}

// common/ds/lru.cc
#define DEFAULT_LRU_SIZE    (500)

// common/debug.hh
#ifndef DEBUG
#define DEBUG    1
#endif

// keymod (in general)
#define KEY_SIZE    (16)

// all typedef go here
typedef long long                                               LL;
typedef unsigned long long                                      ULL;
typedef unsigned int                                            lba_t;
typedef int                                                     disk_id_t;
typedef int                                                     chunk_id_t;
typedef chunk_id_t                                              coding_parm_t;
typedef int                                                     sid_t;
typedef ULL                                                     offset_t;
typedef LL                                                      len_t;
typedef uint32_t                                                chunk_offset_t;
typedef uint32_t                                                chunk_len_t;
typedef chunk_offset_t                                          container_offset_t;
typedef chunk_len_t                                             container_len_t;
typedef chunk_id_t                                              container_id_t;
typedef std::pair<offset_t, len_t>                              off_len_t;
typedef std::pair<offset_t, len_t>                              soff_len_t;
typedef std::pair<container_offset_t, container_len_t>          container_off_len_t;
//typedef std::pair<disk_id_t, lba_t>                             StripeLocation; // diskId -> LBA
typedef std::pair<container_id_t, std::pair<disk_id_t, lba_t> > StripeLocation; // container id, lba_t
typedef Timed2DHotness                                          Hotness;
/* boost shared_mutex, can changed to std::shared_mutex if c++14 available */
typedef boost::shared_mutex RWMutex;

// NVMap Wrapper
#ifndef nv_map
#define nv_map std::map
#endif

#ifndef nv_unordered_map
#define nv_unordered_map std::unordered_map
#endif

/** DRY_RUN mode w/o disk I/O, or run w/ disk I/O **/
#ifdef DRY_RUN
    #undef ACTUAL_DISK_IO
    #define DISKLBA_OUT
    /** disk block size, used for printing LBAs **/
    #define DISK_BLOCK_SIZE    (4096)
#elif defined DRY_RUN_PERF
    #undef ACTUAL_DISK_IO
#else
    #define ACTUAL_DISK_IO
    #undef DISKLBA_OUT
#endif
//#define READ_AHEAD    (64 * 1024)
#define INVALID_VALUE       (-1)
#define INVALID_LBA         (lba_t) (INVALID_VALUE)
#define INVALID_DISK        (disk_id_t) (INVALID_VALUE)
#define INVALID_CHUNK       (chunk_id_t) (INVALID_VALUE)
#define INVALID_CONTAINER   (container_id_t) (INVALID_VALUE)
#define INVALID_SEG         (sid_t) (INVALID_VALUE)
#define INVALID_STRIPE      (sid_t) (INVALID_VALUE)
#define INVALID_LEN         (len_t) (INVALID_VALUE)
#define INVALID_CLEN        (container_len_t) (INVALID_VALUE)

/** default no. of threads, single thread **/
#define NUM_THREAD    1

#endif

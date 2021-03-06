/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation, version 2.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */

#ifndef _FILE_H
#define _FILE_H

#include <malloc.h>

#include <rozofs/rozofs.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/core/ruc_list.h>

#define FILE_CHECK_WORD 0X46696c65

extern int rozofs_bugwatch;
extern uint64_t rozofs_opened_file;
extern exportclt_t exportclt; 

typedef struct dirbuf {
    char *p;
    size_t size;   /**< received size */
    uint8_t eof;   /**< assert to 1 upon reaching end of file */
    uint64_t cookie;  /**< next cookie  */
    uint64_t last_cookie_buf;  /**< cookie of the last entry provided to the caller  */
    size_t   cookie_offset_buf; /**< offset of the entry in the buffer               */
} dirbuf_t;

void rozofs_free_db(dirbuf_t * db);

typedef enum 
{
  BUF_ST_EMPTY = 0,
  BUF_ST_READ_BEFORE,
  BUF_ST_READ_INSIDE,
  BUF_ST_READ_AFTER,
  BUF_ST_WRITE_BEFORE,
  BUF_ST_WRITE_INSIDE,
  BUF_ST_WRITE_AFTER,
  BUF_ST_MAX
} rozofs_buf_read_write_state_e;

 
 
 typedef enum 
 {
    BUF_ACT_COPY_EMPTY = 0,
    BUF_ACT_COPY,
    BUF_ACT_COPY_OVERLAP,
    BUF_ACT_FLUSH_THEN_COPY_NEW,
    BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW,
    BUF_ACT_MAX
 } rozofs_buf_read_write_action_e;


 
 typedef enum 
 {
    BUF_STATUS_DONE = 0,
    BUF_STATUS_WR_IN_PRG ,
    BUF_STATUS_FAILURE ,
    BUF_STATUS_MAX
 } rozofs_buf_read_write_status_e;


typedef struct _rozo_buf_rw_status_t {
    int status;
    int errcode;
} rozo_buf_rw_status_t;



typedef struct file {
    ruc_obj_desc_t pending_rd_list;  /**< used to queue the FUSE contextr for which a read is requested  */
    ruc_obj_desc_t pending_wr_list;  /**< used to queue the FUSE context waiting for flush completed  */
    fid_t fid;
    int   chekWord;
//    mode_t mode;
//    mattr_t attrs;
    exportclt_t *export;
//    sclient_t **storages;
    //char buffer[ROZOFS_BUF_SIZE];
    char *buffer;
    int closing;             /**< assert to 1 when the file has to be closed and there are some disks operation pending */
    int buf_write_wait;
    int buf_write_pending;   /**< number of write requests that are pending */
    int buf_read_pending;    /**< number of read requests that are pending */
    int wr_error;            /**< last write error code                     */
    int buf_read_wait;
    int rotation_counter;/**< Rotation counter on file distribution. Incremented on each rotation */
    int rotation_idx;    /**< Rotation index within the rozo forward distribution */ 
    uint64_t read_pos;  /**< absolute position of the first available byte to read*/
    uint64_t read_from; /**< absolute position of the last available byte to read */
    uint64_t write_pos;  /**< absolute position of the first available byte to read*/
    uint64_t write_from; /**< absolute position of the last available byte to read */
    uint64_t current_pos;/**< Estimated current position in the file */
    /*
    ** File lock stuff
    */
    uint64_t         lock_owner_ref; /**< Owner of the lock when a lock has been set. Used to release any
                                          pending lock at the time of the file close */
    ruc_obj_desc_t   pending_lock;   /**< To queue the context waiting for a blocking lock */
    void           * fuse_req;       /**< Pointer to the saved fuse request when waiting for a blocking lock */
    int              lock_type;      /**< Type of requested lock : EP_LOCK_READ or EP_LOCK_WRITE */   
    int              lock_size;      
    uint64_t         lock_start;     
    uint64_t         lock_stop;
    int              lock_sleep;   
    int              lock_delay;
    int              lock_trc_idx;
    uint64_t         timeStamp;
    uint64_t         read_consistency; /**< To check whether the buffer can be read safely */
    void           * ie;               /**< Pointer ot the ientry in the cache */
    int              write_block_counter;  /**< increment each time a write block is called */
    int              write_block_pending;  /**< asserted when a write must be sent          */
    int              write_block_req;  /**< to force the write towards the metadata service (flush and close)          */
    int              file2create;     /**< assert to one on a write when the attributes indicates a file size of 0    */
    uint64_t         off_wr_start;    /**< geo replication :write offset start  */
    uint64_t         off_wr_end;      /**< geo replication :write offset end  */
    int              pending_read_count; 
    int              open_flags;     /**< flags given at opening time */
#if 0
    char *buffer;
    int buf_write_wait;
    int buf_read_wait;
    uint64_t buf_pos;
    uint64_t buf_from;
#endif
} file_t;

/**
*  structure used on an opened directory
*/
typedef struct dir {
   int      release_requested;  /**< assert to 1 when there a pending readdir    */
   int      readdir_pending;    /**< assert to 1 when there is a pending readdir */
   off_t    off;
   dirbuf_t db; ///< buffer used for directory listing
} dir_t;

/**
*  Reset the start and end offset to address the
   case of the gep-replication
   
   @param file : pointer to the file context
   
   @retval none
*/
static inline void rozofs_geo_write_reset(file_t *file)
{
    file->off_wr_start = 0xFFFFFFFFFFFFFFFF;
    file->off_wr_end = 0;
}

/**
*  Init of the directory structure 

 @param none
 
 @retval popinter to the allocated structure
 */
static inline dir_t * rozofs_dir_working_var_init()
{
 dir_t * dir;
 dir = xmalloc(sizeof (dir_t));
 memset(dir,0,sizeof(dir_t));
 return dir;

}

/**
*  release of the directory structure 

 @param none
 
 @retval popinter to the allocated structure
 */
static inline void rozofs_dir_working_var_release(dir_t *dir_p)
{

 if (dir_p->readdir_pending !=0) 
 {
   dir_p->release_requested = 1;
   return;
 }
 rozofs_free_db(&dir_p->db);
 /*
 ** release any buffer allocated for readdir
 */
 xfree(dir_p);

}

/**
*  Init of the file structure 

 @param file : pointer to the structure
 
 @retval none
 */
static inline file_t * rozofs_file_working_var_init(void * ientry, fid_t fid)
{
    file_t * file;
     
    /*
    ** allocate a context for the file descriptor
    */
    file = xmalloc(sizeof (file_t));
    memcpy(file->fid, fid, sizeof (uuid_t));
    file->buffer   = memalign(4096,exportclt.bufsize * sizeof (char));
    xmalloc_stats_insert(malloc_usable_size(file->buffer));
    file->export   =  &exportclt;   

    /*
    ** init of the variable used for buffer management
    */
    file->chekWord  = FILE_CHECK_WORD;
    file->closing   = 0;
    file->wr_error  = 0;
    file->buf_write_pending = 0;
    file->buf_read_pending  = 0;
    file->read_from  = 0;
    file->read_pos   = 0;
    file->write_from = 0;
    file->write_pos  = 0;
    file->buf_write_wait = 0;
    file->buf_read_wait = 0;    
    file->current_pos = 0;
    file->rotation_counter = 0;
    file->rotation_idx = 0;
    file->lock_owner_ref = 0;
    ruc_listEltInitAssoc(&file->pending_lock,file);
    file->fuse_req = NULL;
    file->lock_type = -1;
    ruc_listHdrInit(&file->pending_rd_list);
    ruc_listHdrInit(&file->pending_wr_list);
    file->read_consistency = 0;
    file->ie = ientry;
    file->write_block_counter = 0;
    file->write_block_req = 0;
    file->write_block_pending = 0;
    file->file2create = 0;
    file->pending_read_count = 0;
    rozofs_geo_write_reset(file);

    rozofs_opened_file++;
    
    return file;
}

/**
*  update the start and end offset to address the
   case of the gep-replication
   
   @param file : pointer to the file context
   @param off: start offset
   @param size: size of the writing
   
   @retval none
*/
static inline void rozofs_geo_write_update(file_t *file,off_t off, size_t size)
{
	
   if (file->off_wr_start > off)
   {
      file->off_wr_start = off;
   }
   if (file->off_wr_end < (off+size))
   {
     file->off_wr_end = off+size;
   }
}


/**
*  Close of file descriptor 

 @param file : pointer to the file decriptor structure
 
 @retval 1  on success
 @retval 0  if pending
 @retval -1 on error
 */
extern void rozofs_clear_ientry_write_pending(file_t *f);
static inline int file_close(file_t * f) {

     if (f==NULL) return -1;

     f->closing = 1;
     /*
     ** Check if there some pending read or write
     */     
     if ((f->buf_write_pending) || (f->buf_read_pending))
     {
       /*
       ** need to wait for end of pending transaction
       */
       return 0;
     }
     if (rozofs_bugwatch) severe("BUGROZOFSWATCH free_ctx(%p) ",f);
     
     /*
     ** Release write_pending in ientry
     ** when this file descriptor occupies it.
     */
     rozofs_clear_ientry_write_pending(f);

     /*
     ** Release all memory allocated
     */
     xfree(f->buffer);
     f->chekWord = 0;
     xfree(f);
     rozofs_opened_file--;
    return 1;
}

/**
* API to return the closing state of a file descriptor

  @param file : file descriptor
  
 @retval 1  closing
 @retval 0  not closed
 */ 
 static inline int rozofs_is_file_closing (file_t * f) 
{
   return f->closing;
}
#endif

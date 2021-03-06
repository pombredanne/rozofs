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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sched.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/profile.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/core/ruc_buffer_debug.h>
#include <rozofs/core/com_cache.h>
#include <rozofs/core/rozofs_throughput.h>

#include "storio_disk_thread_intf.h"
#include "sproto_nb.h"
#include "config.h"
#include "storio_device_mapping.h"
#include "storio_serialization.h"

DECLARE_PROFILING(spp_profiler_t); 
 
static int transactionId = 1; 
int        af_unix_disk_south_socket_ref = -1;
int        af_unix_disk_thread_count=0;
int        af_unix_disk_pending_req_count = 0;

#define MAX_PENDING_REQUEST     64
uint64_t   af_unix_disk_pending_req_tbl[MAX_PENDING_REQUEST];

extern uint64_t   af_unix_disk_parallel_req_tbl[ROZOFS_MAX_DISK_THREADS];
extern uint64_t   af_unix_disk_parallel_req;

struct  sockaddr_un storio_south_socket_name;
struct  sockaddr_un storio_north_socket_name;

 
int storio_disk_thread_create(char * hostname, int nb_threads, int instance_id) ;
 


int storio_throughput_enable = 0;
#define STORIO_RD_CNT 0
#define STORIO_WR_CNT 1
rozofs_thr_cnts_t * storio_cnts[2] = {0};

/*_______________________________________________________________________
* Update a read thoughput counter
*
* @param t        the time in seconds
* @param count    Number of bytes that have been just read
*/
static inline void storio_update_read_counter(uint32_t t, uint64_t count) {
  rozofs_thr_cnt_update_with_time(storio_cnts[STORIO_RD_CNT], count, t);
}
/*_______________________________________________________________________
* Update a write thoughput counter
*
* @param t        the time in seconds
* @param count    Number of bytes that have been just read
*/
static inline void storio_update_write_counter(uint32_t t, uint64_t count) {
  rozofs_thr_cnt_update_with_time(storio_cnts[STORIO_WR_CNT], count, t);
}
/*_______________________________________________________________________
* throughput manual
*
* @param pChar    Where to format the ouput
*/
static inline void man_throughput (char * pChar) {
  pChar += rozofs_string_append(pChar,"Display storio throughput statistics of the last minute.\n");
  pChar += rozofs_string_append(pChar,"  throughput enable     to enable throughput measurements.\n");
  pChar += rozofs_string_append(pChar,"  throughput disable    to disable throughput measurements.\n");
  pChar += rozofs_string_append(pChar,"  throughput [read|write] [col <#col>] [avg]\n");
  pChar += rozofs_string_append(pChar,"    [read|write]\n");
  pChar += rozofs_string_append(pChar,"       set read to only get read counters.\n");
  pChar += rozofs_string_append(pChar,"       set write to only get write counters.\n");
  pChar += rozofs_string_append(pChar,"       when neither read nor write are set all counters are displayed.\n");
  pChar += rozofs_string_append(pChar,"    [col <#col>] request the display on <#col> columns.\n");
  pChar += rozofs_string_append(pChar,"    [avg] to display an average per column.\n");    
}
/*_______________________________________________________________________
* Display throughput counters
*
* @param pChar    Where to format the ouput
*/
static inline void display_throughput_syntax (char * pChar,uint32_t tcpRef, void *bufRef) {
  pChar += rozofs_string_append(pChar,"\nthroughput [enable|disable|read|write|col <#col>|avg]\n");
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
  return;      
}
/*_______________________________________________________________________
* Display throughput counters
*
* @param pChar    Where to format the ouput
*/
void display_throughput (char * argv[], uint32_t tcpRef, void *bufRef) {
  char * pChar = uma_dbg_get_buffer();
  int ret,val,what=0; 
  int avg=0; 

  int i=1;
  while (argv[i] != NULL) {

    if (strcasecmp(argv[i],"enable")==0) {
      storio_throughput_enable = 1;
      pChar += rozofs_string_append(pChar,"throughput measurement enabled\n");
      storio_cnts[STORIO_RD_CNT] = rozofs_thr_cnts_allocate(storio_cnts[STORIO_RD_CNT],"Read");
      storio_cnts[STORIO_WR_CNT] = rozofs_thr_cnts_allocate(storio_cnts[STORIO_WR_CNT],"Write");    
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    }

    if (strcasecmp(argv[i],"disable")==0) {
      storio_throughput_enable = 0;
      rozofs_thr_cnts_free(&storio_cnts[STORIO_RD_CNT]);
      rozofs_thr_cnts_free(&storio_cnts[STORIO_WR_CNT]);
      pChar += rozofs_string_append(pChar,"throughput measurement disabled\n");
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    }  
    
    if (strcasecmp(argv[i],"col")==0) {
      i++;
      if (argv[i] == NULL) return display_throughput_syntax (pChar, tcpRef, bufRef);
      ret = sscanf(argv[i],"%d",&val);
      if (ret != 1) return display_throughput_syntax (pChar, tcpRef, bufRef);
      i++;
      rozofs_thr_set_column(val);
      continue;
    }  
    if (strcasecmp(argv[i],"avg")==0) {  
      i++;
      avg = 1;
      continue;
    }     
         
    if (strcasecmp(argv[i],"read")==0) {  
      i++;
      what |= 1;
      continue;
    }  
    
    if (strcasecmp(argv[i],"write")==0) {       
      i++;
      what |= 2;
      continue;
    }  
    
    pChar += rozofs_string_append(pChar,"\nunexpected parameter ");
    return display_throughput_syntax (pChar, tcpRef, bufRef);  
  }     

  if (storio_throughput_enable == 0) {
    pChar += rozofs_string_append(pChar,"throughput measurement is disabled\n");
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
    return;
  }
  
  rozofs_thr_set_average(avg);

  switch (what) {
    case 1:
      pChar = rozofs_thr_display(pChar, &storio_cnts[STORIO_RD_CNT],1);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    case 2:
      pChar = rozofs_thr_display(pChar, &storio_cnts[STORIO_WR_CNT],1);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
      return;
    default:
      pChar = rozofs_thr_display(pChar, storio_cnts, 2);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
      return; 
  }    
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
  return;   
}
/*_______________________________________________________________________
* Initialize the thoughput measurement service
*
*/
void storio_throughput_counter_init(void) {

  storio_throughput_enable = 0;
    
  /*
  ** Initialize counters
  */
  storio_throughput_enable = 1;
  storio_cnts[STORIO_RD_CNT] = rozofs_thr_cnts_allocate(storio_cnts[STORIO_RD_CNT],"Read");
  storio_cnts[STORIO_WR_CNT] = rozofs_thr_cnts_allocate(storio_cnts[STORIO_WR_CNT],"Write");    
  rozofs_thr_set_column(6); 
  /*
  ** Register the diagnostic function
  */
  uma_dbg_addTopicAndMan("throughput", display_throughput,man_throughput,0); 
}

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
#define new_line(title,empty) { \
  if (lineEmpty) { pChar = pLine;}\
  lineEmpty = empty;\
  pLine = pChar;\
  *pChar++ = '\n';\
  pChar += rozofs_string_padded_append(pChar,25,rozofs_left_alignment,title);\
  *pChar++ = '|';\
}  
    
#define display_val(val){\
  pChar += rozofs_u64_padded_append(pChar, 17, rozofs_right_alignment, val);\
  *pChar++ = ' ';\
  *pChar++ = '|';\
}
  
#define display_div(val1,val2) if (val2==0) { display_val(0)} else { display_val(val1/val2)}
#define display_txt(txt) {\
  pChar += rozofs_string_padded_append(pChar,17, rozofs_right_alignment, txt);\
  *pChar++ = ' ';\
  *pChar++ = '|';\
}

#define display_line_topic(title) \
  new_line(title,0);\
  for (i=startIdx; i<(stopIdx+last); i++) {\
    pChar += rozofs_string_append(pChar,"__________________|");\
  }
    
#define display_line_val(title,val) \
  new_line(title,1);\
  for (i=startIdx; i<stopIdx; i++) {\
    sum.val += p[i].stat.val;\
    display_val(p[i].stat.val);\
  }\
  if (sum.val!=0) { lineEmpty=0; }\
  if (last) { display_val(sum.val);}
    

#define display_line_div(title,val1,val2) \
  new_line(title,1);\
  for (i=startIdx; i<stopIdx; i++) {\
    display_div(p[i].stat.val1,p[i].stat.val2);\
  }\
  if (sum.val1!=0) { lineEmpty=0; }\
  if (last) { display_div(sum.val1,sum.val2); }

 
static char * disk_thread_debug_help(char * pChar) {
  pChar += rozofs_string_append(pChar,"usage:\ndiskThreads reset       : reset statistics\ndiskThreads             : display statistics\n");  
  return pChar; 
}  
#define THREAD_PER_LINE 6
void disk_thread_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  char           *pChar=uma_dbg_get_buffer();
  char           *pLine=pChar;
  int             lineEmpty=0;;
  int i;
  rozofs_disk_thread_ctx_t *p = rozofs_disk_thread_ctx_tb;
  int startIdx,stopIdx;
  rozofs_disk_thread_stat_t sum;
  int                       last=0;
  int doreset=0;
  
  if (argv[1] != NULL) {
    if (strcmp(argv[1],"reset")!=0) {    
      pChar = disk_thread_debug_help(pChar);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
      return;      
    }  
    doreset = 1;
  }
  
  pChar += rozofs_string_append(pChar,"current pending requests = ");
  pChar += rozofs_u32_append(pChar,af_unix_disk_pending_req_count);
  pChar += rozofs_string_append(pChar,"\npending requests table   ");  
  for (i=0; i<MAX_PENDING_REQUEST; i++) {
    if (af_unix_disk_pending_req_tbl[i]==0) {
      continue;
    }
    pChar += rozofs_string_append(pChar," [");
    pChar += rozofs_u32_append(pChar,i);
    pChar += rozofs_string_append(pChar,"]=");    
    pChar += rozofs_u32_append(pChar,af_unix_disk_pending_req_tbl[i]);
    if ((i%8)==0) pChar += rozofs_string_append(pChar,"\n                         ");
  }
  
  pChar += rozofs_string_append(pChar,"\nparallel active requests = ");
  pChar += rozofs_u32_append(pChar,af_unix_disk_parallel_req); 
  pChar += rozofs_string_append(pChar,"\nactive requests table    ");
  for (i=0; i<ROZOFS_MAX_DISK_THREADS; i++) {
    if (af_unix_disk_parallel_req_tbl[i]==0) {
      continue;
    }
    pChar += rozofs_string_append(pChar," [");
    pChar += rozofs_u32_append(pChar,i);
    pChar += rozofs_string_append(pChar,"]=");    
    pChar += rozofs_u32_append(pChar,af_unix_disk_parallel_req_tbl[i]);
    if ((i%8)==0) pChar += rozofs_string_append(pChar,"\n                         ");
  }    
  pChar += rozofs_string_append(pChar,"\n");
     
  
  memset(&sum, 0, sizeof(sum));
  stopIdx  = 0;
  last = 0;
   
  while (last == 0) {
  
    startIdx = stopIdx;
    if ((af_unix_disk_thread_count - startIdx) > THREAD_PER_LINE) {
      stopIdx = startIdx + THREAD_PER_LINE;
    }  
    else {
      stopIdx = af_unix_disk_thread_count;
      last = 1;
    }  
    
    new_line("Thread number",0);
    for (i=startIdx; i<stopIdx; i++) {
      display_val(p[i].thread_idx);
    } 
    if (last) {
      display_txt("Total");
    }   

    display_line_topic("Read Requests");  
    display_line_val("   number", read_count);
    display_line_val("   No such file",read_nosuchfile);
    display_line_val("!! Unknown cid/sid",read_badCidSid);  
    display_line_val("!! error spare",read_error_spare);  
    display_line_val("!! error",read_error);  
    display_line_val("   Bytes",read_Byte_count);      
    display_line_val("   Cumulative Time (us)",read_time);
    display_line_div("   Average Bytes",read_Byte_count,read_count);  
    display_line_div("   Average Time (us)",read_time,read_count);
    display_line_div("   Throughput (MBytes/s)",read_Byte_count,read_time);  

    display_line_topic("Write Requests");  
    display_line_val("   number", write_count);
    display_line_val("!! Unknown cid/sid",write_badCidSid);  
    display_line_val("!! error",write_error);  
    display_line_val("!! no space left",write_nospace);  
    display_line_val("   Bytes",write_Byte_count);      
    display_line_val("   Cumulative Time (us)",write_time);
    display_line_div("   Average Bytes",write_Byte_count,write_count); 
    display_line_div("   Average Time (us)",write_time,write_count);
    display_line_div("   Throughput (MBytes/s)",write_Byte_count,write_time);  

    display_line_topic("Truncate Requests");  
    display_line_val("   number", truncate_count);
    display_line_val("!! Unknown cid/sid",truncate_badCidSid);  
    display_line_val("!! error",truncate_error);  
    display_line_val("   Cumulative Time (us)",truncate_time);
    display_line_div("   Average Time (us)",truncate_time,truncate_count);

    display_line_topic("Repair Requests");  
    display_line_val("   number", diskRepair_count);
    display_line_val("!! Unknown cid/sid",diskRepair_badCidSid);  
    display_line_val("!! error",diskRepair_error);  
    display_line_val("   Bytes",diskRepair_Byte_count);      
    display_line_val("   Cumulative Time (us)",diskRepair_time);
    display_line_div("   Average Bytes",diskRepair_Byte_count,diskRepair_count); 
    display_line_div("   Average Time (us)",diskRepair_time,diskRepair_count);
    display_line_div("   Throughput (MBytes/s)",diskRepair_Byte_count,diskRepair_time);  

    display_line_topic("Remove Requests");  
    display_line_val("   number", remove_count);
    display_line_val("!! Unknown cid/sid",remove_badCidSid);  
    display_line_val("!! error",remove_error);  
    display_line_val("   Cumulative Time (us)",remove_time);
    display_line_div("   Average Time (us)",remove_time,remove_count);  
  
    display_line_topic("Remove chunk Requests");  
    display_line_val("   number", remove_chunk_count);
    display_line_val("!! Unknown cid/sid",remove_chunk_badCidSid);  
    display_line_val("!! error",remove_chunk_error);  
    display_line_val("   Cumulative Time (us)",remove_chunk_time);
    display_line_div("   Average Time (us)",remove_chunk_time,remove_chunk_count);  

    display_line_topic("Start rebuild Requests");  
    display_line_val("   number", rebStart_count);
    display_line_val("!! Unknown cid/sid",rebStart_badCidSid);  
    display_line_val("!! error",rebStart_error);  
    display_line_val("   Cumulative Time (us)",rebStart_time);
    display_line_div("   Average Time (us)",rebStart_time,rebStart_count);  

    display_line_topic("Stop rebuild Requests");  
    display_line_val("   number", rebStop_count);
    display_line_val("!! Unknown cid/sid",rebStop_badCidSid);  
    display_line_val("!! error",rebStop_error);  
    display_line_val("   Cumulative Time (us)",rebStop_time);
    display_line_div("   Average Time (us)",rebStop_time,rebStop_count);  
 
    display_line_topic("");  
    *pChar++= '\n';
    *pChar = 0;
  }

  if (doreset) {
    for (i=0; i<af_unix_disk_thread_count; i++) {
       memset(&p[i].stat,0,sizeof(p[i].stat));
    }
    memset(af_unix_disk_pending_req_tbl,0,sizeof(af_unix_disk_pending_req_tbl));
    memset(af_unix_disk_parallel_req_tbl,0,sizeof(uint64_t)*ROZOFS_MAX_DISK_THREADS);
    pChar += rozofs_string_append(pChar,"Reset done\n");                
  }
  
  uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
}


 /**
 * prototypes
 */
uint32_t af_unix_disk_rcvReadysock(void * af_unix_disk_ctx_p,int socketId);
uint32_t af_unix_disk_rcvMsgsock(void * af_unix_disk_ctx_p,int socketId);
uint32_t af_unix_disk_xmitReadysock(void * af_unix_disk_ctx_p,int socketId);
uint32_t af_unix_disk_xmitEvtsock(void * af_unix_disk_ctx_p,int socketId);

#define DISK_SO_SENDBUF  (300*1024)
#define DISK_SOCKET_NICKNAME "disk_resp_th"
/*
**  Call back function for socket controller
*/
ruc_sockCallBack_t af_unix_disk_callBack_sock=
  {
     af_unix_disk_rcvReadysock,
     af_unix_disk_rcvMsgsock,
     af_unix_disk_xmitReadysock,
     af_unix_disk_xmitEvtsock
  };
  
  /*
**__________________________________________________________________________
*/
/**
  Application callBack:

  Called from the socket controller. 

serial
  @param unused: not used
  @param socketId: reference of the socket (not used)
 
  @retval : always FALSE
*/

uint32_t af_unix_disk_xmitReadysock(void * unused,int socketId)
{

    return FALSE;
}


/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   Called from the socket controller upon receiving a xmit ready event
   for the associated socket. That callback is activeted only if the application
   has replied TRUE in rozofs_fuse_xmitReadysock().
   
   It typically the processing of a end of congestion on the socket

    
  @param unused: not used
  @param socketId: reference of the socket (not used)
 
   @retval :always TRUE
*/
uint32_t af_unix_disk_xmitEvtsock(void * unused,int socketId)
{
   
    return TRUE;
}
/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   receiver ready function: called from socket controller.
   The module is intended to return if the receiver is ready to receive a new message
   and FALSE otherwise

    
  @param unused: not used
  @param socketId: reference of the socket (not used)
 
  @retval : TRUE-> receiver ready
  @retval : FALSE-> receiver not ready
*/

uint32_t af_unix_disk_rcvReadysock(void * unused,int socketId)
{
  return TRUE;
}

/*
**__________________________________________________________________________
*/
/**
  Processes a disk response

   Called from the socket controller when there is a response from a disk thread
   the response is either for a disk read or write
    
  @param msg: pointer to disk response message
 
  @retval :none
*/
void af_unix_disk_response(storio_disk_thread_msg_t *msg) 
{
  storio_device_mapping_t * dev_map_p = NULL;
  storio_disk_thread_request_e   opcode;
  rozorpc_srv_ctx_t            * rpcCtx;
  int                            ret;
  uint64_t                       tic, toc;  
  struct timeval                 tv;  
  rpcCtx = msg->rpcCtx;
  opcode = msg->opcode;
  tic    = msg->timeStart; 

  switch (opcode) {
  
    case STORIO_DISK_THREAD_READ:
    {
      STOP_PROFILING_IO(read,msg->size);
      update_read_detailed_counters(toc - tic);      
      storio_update_read_counter(tv.tv_sec,msg->size);        
      break;
    }  
  
    case STORIO_DISK_THREAD_RESIZE:
    {
      STOP_PROFILING_IO(read,msg->size);
      update_read_detailed_counters(toc - tic);      
      storio_update_read_counter(tv.tv_sec,msg->size);        
      break;
    }  
    
    case STORIO_DISK_THREAD_WRITE:{
      STOP_PROFILING_IO(write,msg->size);
      update_write_detailed_counters(toc - tic);  
      storio_update_write_counter(tv.tv_sec,msg->size);                      
      break;     
    }  
        
    case STORIO_DISK_THREAD_TRUNCATE:
    {
      STOP_PROFILING(truncate);
      break;
    }  
       
    case STORIO_DISK_THREAD_WRITE_REPAIR:
    case STORIO_DISK_THREAD_WRITE_REPAIR2:
    {  
      STOP_PROFILING_IO(repair,msg->size);
      update_write_detailed_counters(toc - tic); 
      break;
    }  
          
    case STORIO_DISK_THREAD_REMOVE:
    {
      STOP_PROFILING(remove);
      break; 
    }  
          
    case STORIO_DISK_THREAD_REMOVE_CHUNK:
    {
      STOP_PROFILING(remove_chunk);
      break;    
    }  
          
    case STORIO_DISK_REBUILD_START:
    {
      STOP_PROFILING(rebuild_start);
      break; 
    }  
          
    case STORIO_DISK_REBUILD_STOP:
    {
      dev_map_p = storio_device_mapping_ctx_retrieve(msg->fidIdx);

      if (dev_map_p != NULL) {
        sp_rebuild_stop_response(dev_map_p, rpcCtx);
      }
      else {
        severe("STORIO_DISK_REBUILD_STOP");
      }	
      STOP_PROFILING(rebuild_stop);      
      break;                  
    }  
          
    default:
      severe("Unexpected opcode %d", opcode);
  }

  /*
  ** Retrieve FID context
  */
  if (dev_map_p == NULL) {
    /* Get from given index in message */
    dev_map_p = storio_device_mapping_ctx_retrieve(msg->fidIdx);
  }
 
  
  if (dev_map_p == NULL) {
    severe("Missing context");
  }
  else { 
    /*
    ** Send waiting request if any
    */
    storio_serialization_end(dev_map_p,rpcCtx) ;	
        
    /*
    ** Put the FID context in the correct list
    ** (i.e running or inactive list)
    */
    storio_device_mapping_ctx_evaluate(dev_map_p);  
  } 
    
  /*
  ** send the response towards the storcli process that initiates the disk operation
  */
  ret = af_unix_generic_send_stream_with_idx((int)rpcCtx->socketRef,rpcCtx->xmitBuf); 
  if (ret == 0) {
    /**
    * success so remove the reference of the xmit buffer since it is up to the called
    * function to release it
    */
    ROZORPC_SRV_STATS(ROZORPC_SRV_SEND);
    rpcCtx->xmitBuf = NULL;
  }
  else {
    ROZORPC_SRV_STATS(ROZORPC_SRV_SEND_ERROR);
  }
    
  rozorpc_srv_release_context(rpcCtx);          
}

/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   Called from the socket controller when there is a message pending on the
   socket associated with the context provide in input arguments.
   
   That service is intended to process a response sent by a disk thread

    
  @param unused: user parameter not used by the application
  @param socketId: reference of the socket 
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/

uint32_t af_unix_disk_rcvMsgsock(void * unused,int socketId)
{
  storio_disk_thread_msg_t   msg;
  int                        bytesRcvd;
  int eintr_count = 0;

  /*
  ** disk responses have the highest priority, loop on the socket until
  ** the socket becomes empty
  */
  while(1) {  
    /*
    ** check if there are some pending requests
    */
    if (af_unix_disk_pending_req_count == 0)
    {
     goto out;
    }
    /*
    ** read the north disk socket
    */
    bytesRcvd = recvfrom(socketId,
			 &msg,sizeof(msg), 
			 0,(struct sockaddr *)NULL,NULL);
    if (bytesRcvd == -1) {
     switch (errno)
     {
       case EAGAIN:
        /*
        ** the socket is empty
        */
        goto out;

       case EINTR:
         /*
         ** re-attempt to read the socket
         */
         eintr_count++;
         if (eintr_count < 3) continue;
         /*
         ** here we consider it as a error
         */
         severe ("Disk Thread Response error too many eintr_count %d",eintr_count);
         goto out;

       case EBADF:
       case EFAULT:
       case EINVAL:
       default:
         /*
         ** We might need to double checl if the socket must be killed
         */
         fatal("Disk Thread Response error on recvfrom %s !!\n",strerror(errno));
         exit(0);
     }

    }
    if (bytesRcvd == 0) {
      fatal("Disk Thread Response socket is dead %s !!\n",strerror(errno));
      exit(0);    
    } 
    af_unix_disk_pending_req_count--;
    if (  af_unix_disk_pending_req_count < 0) af_unix_disk_pending_req_count = 0;
    af_unix_disk_response(&msg); 
  }    
  
out:
  return TRUE;
}


/*
**__________________________________________________________________________
*/
/**
* fill the storio  AF_UNIX name in the global data

  @param hostname
  @param socketname : pointer to a sockaddr_un structure
  
  @retval none
*/
void storio_set_socket_name_with_hostname(struct sockaddr_un *socketname,char *name,char *hostname,int instance_id)
{
  socketname->sun_family = AF_UNIX;  
  char * pChar = socketname->sun_path;
  pChar += rozofs_string_append(pChar,name);
  *pChar++ = '_';
  pChar += rozofs_u32_append(pChar,instance_id);
  *pChar++ = '_';  
  pChar += rozofs_string_append(pChar,hostname);
}

/*
**__________________________________________________________________________
*/
/**
*  Thar API is intended to be used by a disk thread for sending back a 
   disk response (read/write or truncate) towards the main thread
   
   @param thread_ctx_p: pointer to the thread context (contains the thread source socket )
   @param msg: pointer to the message that contains the disk response
   @param status : status of the disk operation
   
   @retval none
*/
void storio_send_response (rozofs_disk_thread_ctx_t *thread_ctx_p, storio_disk_thread_msg_t * msg, int status) 
{
  int                     ret;
  
  msg->status = status;
  
  /*
  ** send back the response
  */  
  ret = sendto(thread_ctx_p->sendSocket,msg, sizeof(*msg),0,(struct sockaddr*)&storio_south_socket_name,sizeof(storio_south_socket_name));
  if (ret <= 0) {
     fatal("storio_send_response %d sendto(%s) %s", thread_ctx_p->thread_idx, storio_south_socket_name.sun_path, strerror(errno));
     exit(0);  
  }
}

/*__________________________________________________________________________
*/
/**
*  Send a disk request to the disk threads
*
* @param fidCtx     FID context
* @param rpcCtx     pointer to the generic rpc context
* @param timeStart  time stamp when the request has been decoded
*
* @retval 0 on success -1 in case of error
*  
*/
int storio_disk_thread_intf_send(storio_device_mapping_t      * fidCtx,
                                 rozorpc_srv_ctx_t            * rpcCtx,
				                 uint64_t       timeStart) 
{
  int                         ret;
  storio_disk_thread_msg_t    msg;
 
  /* Fill the message */
  msg.msg_len          = sizeof(storio_disk_thread_msg_t)-sizeof(msg.msg_len);
  msg.opcode           = rpcCtx->opcode;
  msg.status           = 0;
  msg.transaction_id   = transactionId++;
  msg.fidIdx           = fidCtx->index;
  msg.timeStart        = timeStart;
  msg.size             = 0;
  msg.rpcCtx           = rpcCtx;
  
  /* Send the buffer to its destination */
  ret = sendto(af_unix_disk_south_socket_ref,&msg, sizeof(msg),0,(struct sockaddr*)&storio_north_socket_name,sizeof(storio_north_socket_name));
  if (ret <= 0) {
     fatal("storio_disk_thread_intf_send  sendto(%s) %s", storio_north_socket_name.sun_path, strerror(errno));
     exit(0);  
  }
  
  af_unix_disk_pending_req_count++;
  if (af_unix_disk_pending_req_count<MAX_PENDING_REQUEST) {
    af_unix_disk_pending_req_tbl[af_unix_disk_pending_req_count]++;
  }
  else {
    af_unix_disk_pending_req_tbl[MAX_PENDING_REQUEST-1]++;    
  }  
  return 0;
}

/*
**__________________________________________________________________________
*/

/**
* creation of the AF_UNIX socket that is attached on the socket controller

  That socket is used to receive back the response from the threads that
  perform disk operation (read/write/truncate)
  
  @param socketname : name of the socket
  
  @retval >= 0 : reference of the socket
  @retval < 0 : error
*/
int af_unix_disk_response_socket_create(char *socketname)
{
  int len;
  int fd = -1;
  void *sockctrl_ref;

   len = strlen(socketname);
   if (len >= AF_UNIX_SOCKET_NAME_SIZE)
   {
      /*
      ** name is too big!!
      */
      severe("socket name %s is too long: %d (max is %d)",socketname,len,AF_UNIX_SOCKET_NAME_SIZE);
      return -1;
   }
   while (1)
   {
     /*
     ** create the socket
     */
     fd = af_unix_sock_create_internal(socketname,DISK_SO_SENDBUF);
     if (fd == -1)
     {
       break;
     }
     /*
     ** OK, we are almost done, just need to connect with the socket controller
     */
     sockctrl_ref = ruc_sockctl_connect(fd,  // Reference of the socket
                                                DISK_SOCKET_NICKNAME,   // name of the socket
                                                16,                  // Priority within the socket controller
                                                (void*)NULL,      // user param for socketcontroller callback
                                                &af_unix_disk_callBack_sock);  // Default callbacks
      if (sockctrl_ref == NULL)
      {
         /*
         ** Fail to connect with the socket controller
         */
         fatal("error on ruc_sockctl_connect");
         break;
      }
      /*
      ** All is fine
      */
      break;
    }    
    return fd;
}

/*__________________________________________________________________________
*/
/**
*   entry point for disk response socket polling
*

   @param current_time : current time provided by the socket controller
   
   
   @retval none
*/
void af_unix_disk_scheduler_entry_point(uint64_t current_time)
{
  af_unix_disk_rcvMsgsock(NULL,af_unix_disk_south_socket_ref);
}

/*__________________________________________________________________________
* Initialize the disk thread interface
*
* @param hostname    storio hostname (for tests)
* @param nb_threads  Number of threads that can process the disk requests
*
*  @retval 0 on success -1 in case of error
*/
int storio_disk_thread_intf_create(char * hostname, int instance_id, int nb_threads) {

  /*
  ** Initialize throughput compute diagnostic service
  */
  storio_throughput_counter_init(); 

  af_unix_disk_thread_count = nb_threads;

  /*
  ** init of the AF_UNIX sockaddr associated with the south socket (socket used for disk response receive)
  */
  storio_set_socket_name_with_hostname(&storio_south_socket_name,ROZOFS_SOCK_FAMILY_DISK_SOUTH,hostname, instance_id);
    
  /*
  ** hostname is required for the case when several storaged run on the same server
  ** as is the case of test on one server only
  */   
  af_unix_disk_south_socket_ref = af_unix_disk_response_socket_create(storio_south_socket_name.sun_path);
  if (af_unix_disk_south_socket_ref < 0) {
    fatal("storio_create_disk_thread_intf af_unix_sock_create(%s) %s",storio_south_socket_name.sun_path, strerror(errno));
    return -1;
  }
 /*
  ** init of the AF_UNIX sockaddr associated with the north socket (socket used for disk request receive)
  */
  storio_set_socket_name_with_hostname(&storio_north_socket_name,ROZOFS_SOCK_FAMILY_DISK_NORTH,hostname,instance_id);
  
  uma_dbg_addTopic_option("diskThreads", disk_thread_debug,UMA_DBG_OPTION_RESET); 
  /*
  ** attach the callback on socket controller
  */
  ruc_sockCtrl_attach_applicative_poller(af_unix_disk_scheduler_entry_point);  
   
  return storio_disk_thread_create(hostname, nb_threads, instance_id);
}




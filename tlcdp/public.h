/***************************************************************************************************
public.h:
    Copyright (c) Eisoo Software, Inc.(2004 - 2016), All rights reserved.

Purpose:
    common prototypes and structures used between user-mode and kernel-mode.

Author:
    Bend (cao.dingke@eisoo.com)

Creating Time:
    2015-03-01
***************************************************************************************************/
#ifndef _IO_FILTER_PUBLIC_H_
#define _IO_FILTER_PUBLIC_H_

#include <linux/netlink.h>

#ifndef __packed
#define __packed            __attribute__((__packed__))
#endif

//
/////////////////////////////////////////////////////////Macros Definition/////////////////////////////////////////////////
//
#define DISK_DOS_NAME_LEN   (64)
#define DISK_DEV_NAME_LEN   (32)
#define MAX_SECTION_SIZE    (32*1024*1024)
#define MAX_READ_BLOCK_SIZE (4*1024*1024)       // the size of max read block, for CDP_BITMAP_MODE.
#define SWAP_BUFFER_SIZE    (16*1024*1024)      // the size of swap buffer, for kernel and upper layer swap I/O data.
#define MAX_LOG_ITEM_SIZE   (4*1024)
#define LOG_FILE_HEAD_SIZE  (4*1024)
#define LOG_HEAD_SIZE       (512)
#define LOG_ITEMS_PER_FILE  (((MAX_SECTION_SIZE)-(LOG_FILE_HEAD_SIZE))/((MAX_LOG_ITEM_SIZE)+(LOG_HEAD_SIZE)))
#define LOG_ITEMS_PER_SWAP  (((SWAP_BUFFER_SIZE)-(LOG_FILE_HEAD_SIZE))/((MAX_LOG_ITEM_SIZE)+(LOG_HEAD_SIZE)))

#define DEFAULT_REGION_SIZE         (128*1024)  // kmalloc can not alloc more than 128KB memory in some kernel verison.
#define DEFAULT_BLOCKS_PER_REGION   (DEFAULT_REGION_SIZE << 3)
#define MAX_IO_METADATA_ENTRIES     (100)

#define CDP_CMD_INIT            0x00000001
#define CDP_CMD_STARTUP         0x00000002
#define CDP_CMD_DATA            0x00000003
#define CDP_CMD_DEL             0x00000004
#define CDP_CMD_STOP            0x00000005
#define CDP_CMD_SID             0x00000006
#define CDP_CMD_REINIT          0x00000007
#define CDP_CMD_RESTART         0x00000008
#define CDP_CMD_EXCP            0x00000009
#define CDP_CMD_CONSIS          0x0000000A
#define CDP_CMD_SET_BITMAP      0x00000010
#define CDP_CMD_RESET_BITMAP    0x00000011
#define CDP_CMD_MERGE_BITMAP    0x00000012
#define CDP_CMD_GET_BITMAP      0x00000013
#define CDP_CMD_STATUS          0x00000014

#define CDP_EXCP_NETLINK    (29)                    // driver-defined netlink type for exception handler
#define CDP_NETLINK_TYPE    (30)                    // driver-defined netlink type for control channel
#define CDP_DATA_NETLINK    CDP_NETLINK_TYPE+1      // driver-defined netlink type for data channel

#define DATA_TYPE_IO        0x00000000
#define DATA_TYPE_LABEL     0x00000001

//
// Common Error Code Definitions.
//
#define CDP_RESOURCE_MEM_LACK_WARN          100     // insufficient memory warning
#define CDP_RESOURCE_MEM_LACK_ERROR         101     // insufficient memory error
#define CDP_RESOURCE_DISK_LACK_WARN         102     // insufficient disk space warning
#define CDP_RESOURCE_DISK_LACK_ERROR        103     // log volume space out of limit error
#define CDP_RESOURCE_CREATE_LOG_ERROR       104     // fail to create cache file
#define CDP_UPDATE_BITMAP_VALID             105
#define CDP_UPDATE_BITMAP_INVALID           106
#define CDP_NO_MORE_UPDATE_DATA             107
#define CDP_END_OF_UPDATE_BITMAP            108
#define CDP_DISK_STATUS_UNKNOWN             109     // can not get the status of disk
#define CDP_DISK_NO_SPACE                   110     // insufficient log volume space error
#define ERROR_QUEUE_NULL                    111
#define ERROR_MAKE_FN_NULL                  112
#define CDP_INSTANCE_ALREADLY_USED          113


//
/////////////////////////////////////////////////////Structures Definition//////////////////////////////////////////////
//

//
// partition extents struct, do not used in current version.
//
struct cdp_filter_part_extents {
    __u16 major_no;
    __u16 minor_no;
    __u32 sec_size;         // sector size
    __u32 bl_size;          // block size
    __u64 extent_start;     // start offset, in sector, of this extent(this device)
    __u64 extent_length;    // size, in sector, of this extent(this device)
}__packed;


struct cdp_filter_jbd_range {
    __u64 start_offset;     // the start offset of JBD.
    __u64 end_offset;       // the end offset of JBD.
};

//
// request structures are conjunction with command CDP_CMD_INIT/CDP_CMD_REINIT.
//
struct cdp_filter_init_info {
    __u16 major_no;                         // major device number
    __u16 minor_no;                         // minor device number
    __u32 sec_size;                         // sector size
    __u32 bl_size;                          // block size
    __u32 bl_count;                         // total block count of this monitored device
    __u8 mount_point[DISK_DOS_NAME_LEN];    // the name of mount point, e.g: /a
    __u8 dev_name[DISK_DOS_NAME_LEN];       // the name of device, e.g: /dev/sda1
    __u8 log_path[DISK_DOS_NAME_LEN];       // the name of log path, e.g: /b
    __u8 log_dev_name[DISK_DOS_NAME_LEN];   // the name of log device, e.g: /dev/sda2
    struct cdp_filter_jbd_range jbd_range;   // the range of JBD.
}__packed;

struct cdp_filter_src_limit {
    long long mem_limit;        // max available size, in GB, of memory
    long long disk_limit;       // max available size, in GB, of disk space
    int sync_limit;        // sync frequency, in seconds, of sync interval
};

struct cdp_filter_io_metadata {
    __u64 offset;      // the offset of this io.
    __u32 length;      // the length of this io.
};

//
// value of hdr.nlmsg_seq is meaningless when process CDP_CMD_INIT command.
// while hdr.nlmsg_seq is equivalent to the returned OBJECT ID by process CDP_CMD_INIT command successfully
// when process CDP_CMD_REINIT command
//
struct cdp_filter_init_request {
    struct nlmsghdr hdr;
    __u32 item_cnt;                         // elements contained in init_info
    __u32 job_id;                           // cdp job id
    __u32 work_mode;                        // the work mode of cdp driver.
    __u8 ip_addr[20];
    struct cdp_filter_src_limit src_limit;
    struct cdp_filter_init_info init_info[];
};

//
// request structure is conjunction with command CDP_CMD_STARTUP/CDP_CMD_STARTUP.
//
typedef struct cdp_filter_startup_request {
    struct nlmsghdr hdr;
    __u32 task_id;
} cdp_filter_startup_request;

//
// request structure is conjunction with command CDP_CMD_DEL and CDP_CMD_STOP.
//
typedef cdp_filter_startup_request cdp_filter_del_stop_request;

//
// request structure is conjunction with command CDP_CDM_SID.
//
typedef cdp_filter_del_stop_request cdp_filter_sid_request;

//
// request structure is conjunction with command CDP_CMD_CONSIS.
//
typedef cdp_filter_sid_request cdp_filter_consistent_flag_request;

typedef cdp_filter_consistent_flag_request cdp_filter_set_bitmap_request;

typedef cdp_filter_set_bitmap_request cdp_filter_reset_bitmap_request;

typedef cdp_filter_reset_bitmap_request cdp_filter_merge_bitmap_request;

//
// request structure is conjunction with command CDP_CMD_DATA, 
// this request is always sent by kernel driver and having no corresponding response request.
//
struct cdp_filter_data_request {
    __u32 task_id;
    __u8 file_name[DISK_DOS_NAME_LEN];
};

struct cdp_filter_data_request_user {
    struct nlmsghdr hdr;
    __u32 task_id;
    __u8 file_name[DISK_DOS_NAME_LEN];
};

struct cdp_filter_bitmap_request {
    struct nlmsghdr hdr;
    __u32 task_id;
    struct cdp_filter_io_metadata io_metadata_entries[MAX_IO_METADATA_ENTRIES];
};

struct cdp_filter_bitmap_response {
    __u32 task_id;
    struct cdp_filter_io_metadata io_metadata_entries[MAX_IO_METADATA_ENTRIES];
};

typedef struct cdp_filter_bitmap_request_user {
    struct nlmsghdr hdr;
    __u32 task_id;
    __u32 volume_Id;
} cdp_filter_bitmap_request_user ;

struct cdp_filter_direct_data_request {
    struct nlmsghdr hdr;
    __u32 task_id;
    __u64 kernel_virt_addr;
};

struct cdp_filter_update_data_request {
    struct nlmsghdr hdr;
    __u32 task_id;
    __u64 kernel_virt_addr;
};

// common respond structure for all type of requests.
//
struct cdp_filter_response {
    __u32 task_id;
    __s32 error_no;             // result code, 0=success
};

struct cdp_filter_response_user {
    struct nlmsghdr hdr;
    __u32 task_id;
    __s32 error_no;             // result code, 0=success
};

//
// structures associate with I/O log file and data item.
//
struct cdp_filter_item_head {
    __u64 io_timestamp;                     // the timestamp of I/O.
    __u64 item_id;                          // cdp log item id
    __u64 data_offset;                      // offset, in byte, of this data item
    __u32 data_length;                      // length, in byte, of this data item
    __u32 item_type;                        // DATA_TYPE_IO or DATA_TYPE_LABEL
    __u32 volume_index;                     // volume index
    __u32 job_id;                           // cdp job id
    __u8 mount_point[DISK_DOS_NAME_LEN];    // mount point of this volume
    __u8 reserved[(LOG_HEAD_SIZE-sizeof(__u64)*3-sizeof(__u32)*4-sizeof(__u8)*DISK_DOS_NAME_LEN)];
}__packed;

struct cdp_filter_file_head {
    __u32 first_item_no;            // id of the first log item need to be deal with
    __u32 copied_items;             // set by the user-mode caller
    __u32 items;                    // number of log items in current log file.
    __u8 reserved[(LOG_FILE_HEAD_SIZE-3*sizeof(__u32))];
}__packed;

#endif // _IO_FILTER_PUBLIC_H_

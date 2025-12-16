/***************************************************************************************************
cdp_filter.h:
    Copyright (c) EISOO Software, Inc.(2006 - 2016), All rights reserved

Purpose:
    inner prototypes and structures for cdp_filter driver

Author:
    Thomas(li.zhongwen@eisoo.com)

Creating Time:
    2013-06-13
***************************************************************************************************/
#ifndef _IO_FILTER_PROTOTYPES_H_
#define _IO_FILTER_PROTOTYPES_H_

#include <linux/version.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

//
// define macro to support and compatible different Linux kernel version
//
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0))
#define k_4_x_x
#if (LINUX_VERSION_CODE == KERNEL_VERSION(4, 19, 90))
#define k_4_19_90
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(4, 18, 0))
#define k_4_18_0
#endif
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(3, 10, 0))
#define k_3_10_0
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 32))
#define k_2_6_32
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 27))
#define k_2_6_27
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 26))
#define k_2_6_26
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 24))
#define k_2_6_24
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
#define k_2_6_18
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 16))
#define k_2_6_16
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 13))
#define k_2_6_13
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 9))
#define k_2_6_9
#else
#define 
#endif

#if defined(k_4_x_x) || defined(k_3_10_0) || defined(k_2_6_32) 
#include <linux/semaphore.h> 
#else
#include <asm/semaphore.h>
#endif

#include "public.h"

#define CDP_MAX_VOLUMES_PER_TASK    12

#define CDP_DISK_NON_MONITOR    0x00000000
#define CDP_DISK_IN_MONITOR     0x00000001

#define CDP_IS_PART 0x00000000
#define CDP_IS_DISK 0x00000001

#ifndef ISDIGIT
#define ISDIGIT(c)  ( ((c) >= '0') && ((c) <= '9') )
#endif

#ifndef bool
#define bool unsigned char
#endif

//
// add type definition for 2.6.16 and 2.6.18-53 versions to solve compiler issues.
//
#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#ifndef SEEK_END
#define SEEK_END 2
#endif

#ifndef true 
#define true 1
#endif

#ifndef false
#define false 0
#endif

/*
 * cdp work mode.
 */
typedef enum _cdp_work_mode {
    CDP_CACHED_MODE     = 0x0,
    CDP_NO_CACHED_MODE  = 0x1,
    CDP_MIRROR_MODE     = 0x2,
    CDP_BITMAP_MODE     = 0x4,
} cdp_work_mode;

/*
 * cdp task status
 */
typedef enum _cdp_status {
    CDP_STATE_NOINIT        = 0x00000000,
    CDP_STATE_INITIALIZED   = 0x00000001,
    CDP_STATE_PATH_SETTED   = 0x00000002,
    CDP_STATE_STARTED       = 0x00000004,
    CDP_STATE_PATH_DELETED  = 0x00000008,
    CDP_STATE_QUITED        = 0x00000010,
    CDP_STATE_STOPPED       = 0x00000020,
    CDP_STATE_PAUSE         = 0x00000040,
    CDP_STATE_RESUME        = 0x00000080,
    CDP_STATE_SENDE	        = 0X00000100,
} cdp_status;

typedef struct _cdp_bitmap {
    __u32   available_flag;             // 1 mean available and 0 mean unavailable.
    __u32   used_block_count;           // the count of blocks has been used.
    __u32   block_size;                 // the block size of a bit represent, equal to cluster size in current version.
    __u32   blocks_per_region;          // blocks number each buffer represents
    __u32   region_size;                // size, in byte, of each region in Buffer
    __u32   region_count;               // the number of region contains in Buffer.
    __u32   current_seek_region;        // current seek region.
    __u32   current_seek_lcn;           // current seek LCN of current region.
    __u8    **buffer;                   // buffer list contains all the regions
    __u8    **swap_buffer;              // as buffer, but SwapBuffer used for upper layer get bitmap during a cdp period.
    spinlock_t cdp_bitmap_lock;
} cdp_bitmap, *pcdp_bitmap;

struct cdp_filter_dev_info {
    bool is_disk;
    __u16 major;                            // the major number of device.
    __u16 minor;                            // the minor number of device.
    __u8 flag;                              // hook flag, used to recover request_queue and make_request_fn.
    __u8 dev_name[DISK_DOS_NAME_LEN];       // the name of device.
    dev_t dev_no;                           // connecting key between monitor_info and dev_info
    struct gendisk *disk;
    struct hd_struct *part;
    struct request_queue *queue;            // origin request_queue
    make_request_fn *make_request_fn;       // origin make_request_fn
};

struct cdp_filter_monitor_info {
    dev_t dev_no;                           // connecting key between monitor_info and dev_info.
    __u32 sec_size;                         // the sector size of device.
    __u32 bl_size;                          // the block size of device.
    __u32 bl_count;                         // total block count of this monitored device
    __u8 dev_name[DISK_DOS_NAME_LEN];       // the name of monitor device, e.g: /dev/sda1
    __u8 mount_point[DISK_DOS_NAME_LEN];    // the mount point name of monitor device, e.g: /a
    __u8 log_path[DISK_DOS_NAME_LEN];       // the mount point name of log device, e.g: /b
    __u8 log_dev_name[DISK_DOS_NAME_LEN];   // the name of log device, e.g: /dev/sda2
    pcdp_bitmap pupdate_bitmap;
    struct cdp_filter_jbd_range jbd_range;   // the range of JBD.
    struct block_device *device;            // for convenience only
};

struct cdp_filter_task_info {
    __u8 task_id;                           // task unique id
    __u32 pid;                              // process id, identify message receiver
    __u64 log_id;
    __u64 sid;
    long long mem_used;                     // the memory size used by cache I/O data.
    __u32 item_cnt;                         // the I/O count in current log file.
    __u32 item_cnt_old;                     // notify I/O count to upper layer, used by cdp_filter_event_notifier thread.
    loff_t offset;
    __u8 log_path[DISK_DOS_NAME_LEN];       // the mount point name of log device.
    __u8 log_file_name[DISK_DOS_NAME_LEN];  // the name of current I/O log file.
    bool thread_state;
    bool need_update;                       // notify upper layer flag, used by cdp_filter_event_notifier thread.
    bool flag;                              // hook flag
    bool success_last_time;                  // for CDP_BITMAP_MODE

    __u8* swap_buffer;
    __u32 swap_buffer_offset;               // current swap buffer write offset

    struct task_struct *io_thread;
    struct task_struct *notify_thread;
    struct semaphore thread_mutex;          // semaphore for update head of I/O log file.
    struct semaphore io_list_sem;
    struct semaphore swap_buffer_sem;
    struct file *log_file_handle;
    struct cdp_filter_file_head *log_file_head;
    struct cdp_filter_src_limit src_limit;   // memory and disk space limit(in bytes)
    spinlock_t io_list_lock;
    spinlock_t sid_list_lock;
    spinlock_t swap_buffer_lock;
    struct list_head io_list;               // list head for I/O package
    struct list_head sid_list;
    wait_queue_head_t sid_wait_queue;

    __u8 monitor_infos;                     // number of element contains in monitor_info array.
    __u8 monitor_infos_inc;                 // number of added monitor_info element, used by update monitor device.
    struct cdp_filter_monitor_info *monitor_info;
    __u32 job_id;                           // cdp job id
    cdp_status status;                      // the status of current cdp task.
    cdp_work_mode work_mode;                // the work made of current cdp task.
    struct cdp_filter_io_metadata *io_metadata_entries;
};

struct cdp_filter_io_pack {
    __u64 io_timestamp;                     // the timestamp of I/O pack.
    __u64 data_offset;                      // the offset of I/O pack, relative disk device.
    __u32 data_length;                      // the length of I/O pack.
    __u32 item_type;
    __u8 mount_point[DISK_DOS_NAME_LEN];    // the mount point name of I/O write device.
};

struct cdp_filter_data_info {
    struct list_head list;
    __u64 sid;
    __u32 volume_index;                     // the index of volume in cdp task
    __u32 job_id;                           // cdp job id
    __u32 residue_io_data_len;
    __u32 io_data_buffer_offset;
    struct cdp_filter_io_pack item_head;
    __u8 *data;
};

struct cdp_filter_sid {
    struct list_head list;
    __u64 sid;
};

#undef IO_DEBUG
#ifdef __KERNEL__
#ifdef TRANS_DEBUG
#define IO_DEBUG(fmt, arg...)    \
    printk(KERN_DEBUG "cdp_filter(debug): " fmt, ## arg);
#else
#define IO_DEBUG(fmt, arg...) while (0) { }
#endif //TRANS_DEBUG
#define IO_MSG(fmt, arg...)    \
    printk(KERN_INFO "cdp_filter(info): " fmt, ## arg)
#define IO_WARN(fmt, arg...)    \
    printk(KERN_WARNING "cdp_filter(warn): " fmt, ## arg)
#define IO_ERROR(fmt, arg...)    \
    printk(KERN_ALERT "cdp_filter(error): " fmt, ## arg)
#else
#ifdef TRANS_DEBUG
#define IO_DEBUG(fmt, arg...)    \
    printf(stderr, fmt, ## arg)
#else
#define IO_DEBUG(fmt, arg...) while (0) { }
#endif //TRANS_DEBUG
#define IO_MSG(fmt, arg...)    \
    printf(stderr, fmt, ## arg)
#define IO_WARN(fmt, arg...)    IO_DEBUG(fmt, arg...)
#define IO_ERROR(fmt, arg...)   IO_DEBUG(fmt, arg...)
#endif // __KERNEL__

#if defined(k_2_6_26) || defined(k_2_6_18) || defined(k_2_6_16)
//
// Check whether this bio carries any data or not. A NULL bio is allowed.
//
static inline int bio_has_data(struct bio *bio)
{
    return bio && bio->bi_io_vec != NULL;
}
#endif

//
// function forward declare.
//

void
kernel_netlink_data_notify(struct cdp_filter_task_info *task_info);

void
kernel_netlink_response(struct sock *sock, __u32 type, __u32 taskid, __u32 pid, __s32 errno);

#endif // _IO_FILTER_PROTOTYPES_H_

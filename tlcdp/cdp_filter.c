/***************************************************************************************************
cdp_filter.c:
    Copyright (c) EISOO Software, Inc.(2004 - 2016), All rights reserved.

Purpose:
    source file of cdp_filter driver.

Author:
    Thomas(li.zhongwen@eisoo.com)

Creating Time:
    2013-06-13
***************************************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/bitops.h>
#include<linux/inet.h>
#include<linux/inet.h>


#include "cdp_filter.h"

MODULE_DESCRIPTION("Linux Block I/O Filter 6.0.13.0");
MODULE_AUTHOR("EDRIVER TEAM");
MODULE_LICENSE("GPL");

#define SRC_GRAULAR     (1024*1024*1024L) // convent GB to Byte.
#define MAX_TASK_NUM    BITS_PER_LONG
#define MAX_DEV_NUM     128 
#define TASK_MAP_LEN    ((MAX_TASK_NUM) + (BITS_PER_LONG) - 1)/(BITS_PER_LONG) //1

#define DEV_MAJOR_NUM           8
#define MD_MAJOR_NUM            9
#define LVM_MAJOR_NUM           253
#define MDP_MAJOR_NUM           259
#define DEV_SCSI_MAJOR_NUM65    65
#define DEV_SCSI_MAJOR_NUM66    66
#define DEV_SCSI_MAJOR_NUM67    67
#define DEV_SCSI_MAJOR_NUM68    68
#define DEV_SCSI_MAJOR_NUM69    69
#define DEV_SCSI_MAJOR_NUM70    70
#define DEV_SCSI_MAJOR_NUM71    71
#define DEV_SCSI_MAJOR_NUM136   136
#define DEV_SCSI_MAJOR_NUM137   137
#define DEV_SCSI_MAJOR_NUM138   138
#define DEV_SCSI_MAJOR_NUM139   139
#define DEV_SCSI_MAJOR_NUM140   140
#define DEV_SCSI_MAJOR_NUM141   141
#define DEV_SCSI_MAJOR_NUM142   142
#define DEV_SCSI_MAJOR_NUM143   143
#define DEV_SCSI_MAJOR_NUM202   202
#define DEV_SCSI_MAJOR_NUM252   252

#define DEV_MAJOR_NUM_STR           "   8"
#define MD_MAJOR_NUM_STR            "   9"
#define LVM_MAJOR_NUM_STR           " 253"
#define MDP_MAJOR_NUM_STR           " 259"
#define DEV_SCSI_MAJOR_NUM65_STR    " 65"
#define DEV_SCSI_MAJOR_NUM66_STR    " 66"
#define DEV_SCSI_MAJOR_NUM67_STR    " 67"
#define DEV_SCSI_MAJOR_NUM68_STR    " 68"
#define DEV_SCSI_MAJOR_NUM69_STR    " 69"
#define DEV_SCSI_MAJOR_NUM70_STR    " 70"
#define DEV_SCSI_MAJOR_NUM71_STR    " 71"
#define DEV_SCSI_MAJOR_NUM136_STR   " 136"
#define DEV_SCSI_MAJOR_NUM137_STR   " 137"
#define DEV_SCSI_MAJOR_NUM138_STR   " 138"
#define DEV_SCSI_MAJOR_NUM139_STR   " 139"
#define DEV_SCSI_MAJOR_NUM140_STR   " 140"
#define DEV_SCSI_MAJOR_NUM141_STR   " 141"
#define DEV_SCSI_MAJOR_NUM142_STR   " 142"
#define DEV_SCSI_MAJOR_NUM143_STR   " 143"
#define DEV_SCSI_MAJOR_NUM252_STR   " 252"
#define DEV_SCSI_MAJOR_NUM202_STR   " 202"

/*
you can get partitions information from /proc/partitions.
# cat /proc/partitions
major minor  #blocks  name

8   0   20971520    sda
8   1   305203      sda1
8   2   18595237    sda2
8   3   2064352     sda3
8   16  10485760    sdb
8   17  10482381    sdb1
259 0   151998      md0p1
259 1   155172      md0p2

note: 259 is the major device code for software raid partition, do not support yet.
*/
static struct sock *__netlink_sk;
static struct sock *__netlink_data_sk;
static struct sock *__netlink_excep_sk;
static __u64 __logitemcount;
static struct socket *sock;

/**
 * __dev_list is logically divided into four parts: 
 * the left part(256 slots) stores ordinary IDE/SCSI disks/partitions which major device number is 8, 
 * it can store 8 disk devices and 120 partition devices totally
 * the medium part(64 slots) stores lvm devices which major device number is 253
 * it can store 64 lvm logical devices totally
 * the right part(64 slots) stores md devices which major device number is 9
 * it also can store 64 md logical devices totally


* intuitive diagram as following:
*************************************************************************************************
*   256                       *         64          *           64          *       128         *
*   IDE/SCSI disks/partitions * lvm logical devices * md logical partitions * md logical disks  *
*************************************************************************************************

name (major, slot[0-512))
scsi (8, [0,256))
LVM (253, [256,256+64))
MDP (259, [256+64,256+128))
MD (9, [256+128,512))
 */
static struct cdp_filter_dev_info *__dev_list[MAX_DEV_NUM];

static unsigned long __task_bitmap[TASK_MAP_LEN];
static struct cdp_filter_task_info *__task_list[MAX_TASK_NUM];
static int __task_cnt;

#if defined(k_2_6_9) || defined(k_2_6_13) || defined(k_2_6_16) || defined(k_2_6_18)
    DECLARE_MUTEX(__netlink_sem);
#endif


static int connect_send_recv(struct cdp_filter_init_request* init_request)
{
    struct sockaddr_in s_addr;
    unsigned short port_num = 6004;
    int ret = 0;

    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port_num);
    s_addr.sin_addr.s_addr = in_aton(init_request->ip_addr);
    IO_MSG("(%d) ip_addr %s \n", __LINE__, init_request->ip_addr);

    sock = (struct socket *)kmalloc(sizeof(struct socket), GFP_KERNEL);
    if (IS_ERR(sock)) {
        IO_ERROR("(%d) create sock, failed to allocate memory for sock, the ERROR CODE is: %d.\n", 
                    __LINE__, ret = PTR_ERR(sock));
        return ret;
    }

    // 创建一个sock, &init_net是默认网络命名空间
#if defined(k_4_x_x)
    // 4.x以上版本
    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
#else
    ret = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
#endif
    if (ret < 0) {
        IO_ERROR("(%d) client:socket create error!\n", __LINE__);
        kfree(sock);
        return ret;
    }
    IO_MSG("(%d) client: socket create ok!\n", __LINE__);
    // 连接
    ret = sock->ops->connect(sock, (struct sockaddr *)&s_addr, sizeof(s_addr), 0);
    if (ret != 0) {
        IO_ERROR("(%d) client: connect error!\n", __LINE__);
        goto out_release_sock;
    }

    IO_MSG("(%d) client: socket connect ok!\n", __LINE__);

    return 0;

out_release_sock:
    if (sock) {
        kernel_sock_shutdown(sock, SHUT_RDWR);
        sock_release(sock);
        sock = NULL;
    }
    return ret;
}


static bool 
init_update_bitmap (struct cdp_filter_task_info *task_info, int dev_id)
{
    IO_MSG("(%d) update bitmap init.\n", __LINE__);

    if (NULL != task_info->monitor_info[dev_id].pupdate_bitmap) {
        /* update bitmap has been init already */
        IO_WARN("(%d) update bitmap has been init already.\n", __LINE__);
        return true;
    }

    /* allocate memory for update bitmap */
    task_info->monitor_info[dev_id].pupdate_bitmap = (pcdp_bitmap)kmalloc(sizeof(cdp_bitmap), GFP_KERNEL);
    if (NULL == task_info->monitor_info[dev_id].pupdate_bitmap) {
        IO_ERROR("(%d) allocate memory for update bitmap failed.\n", __LINE__);
        return false;
    }

    /* 
     * initialize update bitmap 
     */
    task_info->monitor_info[dev_id].pupdate_bitmap->available_flag = 1;
    task_info->monitor_info[dev_id].pupdate_bitmap->used_block_count = 0;
    task_info->monitor_info[dev_id].pupdate_bitmap->block_size = task_info->monitor_info[dev_id].bl_size;
    task_info->monitor_info[dev_id].pupdate_bitmap->region_size = DEFAULT_REGION_SIZE;
    task_info->monitor_info[dev_id].pupdate_bitmap->blocks_per_region = DEFAULT_BLOCKS_PER_REGION;
    task_info->monitor_info[dev_id].pupdate_bitmap->region_count = (task_info->monitor_info[dev_id].bl_count / DEFAULT_BLOCKS_PER_REGION + 1);
    task_info->monitor_info[dev_id].pupdate_bitmap->current_seek_lcn = 0;
    task_info->monitor_info[dev_id].pupdate_bitmap->current_seek_region = 0;
    task_info->monitor_info[dev_id].pupdate_bitmap->buffer = NULL;
    task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer = NULL;
    spin_lock_init(&task_info->monitor_info[dev_id].pupdate_bitmap->cdp_bitmap_lock);

    task_info->monitor_info[dev_id].pupdate_bitmap->buffer = (__u8**)kmalloc(sizeof(__u8*) * task_info->monitor_info[dev_id].pupdate_bitmap->region_count, GFP_KERNEL);
    if (NULL == task_info->monitor_info[dev_id].pupdate_bitmap->buffer) {
        IO_ERROR("(%d) allocate memory for update bitmap buffer failed.\n", __LINE__);
        goto init_bitmap_failed;
    }
    memset(task_info->monitor_info[dev_id].pupdate_bitmap->buffer, 0, sizeof(__u8*) * task_info->monitor_info[dev_id].pupdate_bitmap->region_count);

    task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer = (__u8**)kmalloc(sizeof(__u8*) * task_info->monitor_info[dev_id].pupdate_bitmap->region_count, GFP_KERNEL);
    if (NULL == task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer) {
        IO_ERROR("(%d) allocate memory for update bitmap swap buffer failed.\n", __LINE__);
        goto init_bitmap_failed;
    }
    memset(task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer, 0, sizeof(__u8*) * task_info->monitor_info[dev_id].pupdate_bitmap->region_count);

    IO_MSG("(%d) update bitmap init successfully.\n", __LINE__);
    return true;

init_bitmap_failed:

    if (NULL != task_info->monitor_info[dev_id].pupdate_bitmap) {
        if (NULL != task_info->monitor_info[dev_id].pupdate_bitmap->buffer) {
            kfree(task_info->monitor_info[dev_id].pupdate_bitmap->buffer);
            task_info->monitor_info[dev_id].pupdate_bitmap->buffer = NULL;
        }

        if (NULL != task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer) {
            kfree(task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer);
            task_info->monitor_info[dev_id].pupdate_bitmap->swap_buffer = NULL;
        }

        kfree(task_info->monitor_info[dev_id].pupdate_bitmap);
        task_info->monitor_info[dev_id].pupdate_bitmap = NULL;
    }

    IO_MSG("(%d) update bitmap init failed.\n", __LINE__);
    return false;
}

static int 
set_update_bitmap (pcdp_bitmap pupdate_bitmap, __u64 data_offset, __u32 data_length, int bit_value)
{
    __u32 region_index;
    int update_length = (int)(data_length + do_div(data_offset, pupdate_bitmap->block_size));
    __u32 start_block_index = (__u32)data_offset;

    IO_DEBUG("(%d) set update bitmap, start_block_index: %u, length: %u.\n", __LINE__, start_block_index, data_length);

    if (NULL == pupdate_bitmap
        || NULL == pupdate_bitmap->buffer) {
        IO_ERROR("(%d) invalid parameter.\n", __LINE__);
        return -1;
    }

    region_index = start_block_index / pupdate_bitmap->blocks_per_region;
    if (region_index >= pupdate_bitmap->region_count) {
        IO_ERROR("(%d) region index %u out of limit.\n", __LINE__, region_index);
        return -1;
    }

    if (NULL == pupdate_bitmap->buffer[region_index]) {
        if (!bit_value) {
            /* reset */
            return 0;
        }
        else {
            pupdate_bitmap->buffer[region_index] = (__u8*)kmalloc(pupdate_bitmap->region_size, GFP_KERNEL);
            if (NULL == pupdate_bitmap->buffer[region_index]) {
                IO_ERROR("(%d) allocate memory for update bitmap buffer failed.\n", __LINE__);
                return -1;
            }
            memset(pupdate_bitmap->buffer[region_index], 0, pupdate_bitmap->region_size);
        }
    }

    do {
        start_block_index %= pupdate_bitmap->blocks_per_region;
        if (bit_value) {
            /* set bit */
            ((__u32*)pupdate_bitmap->buffer[region_index])[start_block_index / 32] |= (1 << (start_block_index % 32));
            ++pupdate_bitmap->used_block_count;
        }
        else {
            /* reset bit */
            ((__u32*)pupdate_bitmap->buffer[region_index])[start_block_index / 32] &= ~(1 << (start_block_index % 32));
            --pupdate_bitmap->used_block_count;
        }

        ++start_block_index;

        update_length -= pupdate_bitmap->block_size;

        if (start_block_index == pupdate_bitmap->blocks_per_region
            && update_length > 0) {
                /* current I/O cross region */
                ++region_index;

                if (NULL == pupdate_bitmap->buffer[region_index]) {
                    if (!bit_value) {
                        /* reset */
                        return 0;
                    }
                    else {
                        pupdate_bitmap->buffer[region_index] = (__u8*)kmalloc(pupdate_bitmap->region_size, GFP_KERNEL);
                        if (NULL == pupdate_bitmap->buffer[region_index]) {
                            IO_ERROR("(%d) allocate memory for update bitmap buffer failed.\n", __LINE__);
                            return -1;
                        }
                        memset(pupdate_bitmap->buffer[region_index], 0, pupdate_bitmap->region_size);
                    }
                }
        }
    } while (update_length > 0);

    return 0;
}

static int 
swap_update_bitmap (pcdp_bitmap pupdate_bitmap) 
{
    if (NULL != pupdate_bitmap 
        && NULL != pupdate_bitmap->buffer 
        && NULL != pupdate_bitmap->swap_buffer) {
        __u8** temp_buffer = pupdate_bitmap->buffer;
        pupdate_bitmap->buffer = pupdate_bitmap->swap_buffer;
        pupdate_bitmap->swap_buffer = temp_buffer;
    }

    return 0;
}

static int 
reset_update_bitmap (pcdp_bitmap pupdate_bitmap) 
{
    if (NULL != pupdate_bitmap 
        && NULL != pupdate_bitmap->swap_buffer) {
        int i;
        for ( i = 0; i < pupdate_bitmap->region_count; ++i ) {
            if ( NULL != pupdate_bitmap->swap_buffer[i] ) {
                memset(pupdate_bitmap->swap_buffer[i], 0, pupdate_bitmap->region_size);
            }
        }
    }

    return 0;
}

static int 
merge_update_bitmap (pcdp_bitmap pupdate_bitmap) 
{
    if (NULL != pupdate_bitmap 
        && NULL != pupdate_bitmap->buffer 
        && NULL != pupdate_bitmap->swap_buffer) {
        int i;
        for ( i = 0; i < pupdate_bitmap->region_count; ++i ) {
            if ( NULL != pupdate_bitmap->swap_buffer[i] ) {
                if ( NULL == pupdate_bitmap->buffer[i] ) {
                    /* current region has any update data mark by Buffer field, exchange pointer direct. */
                    pupdate_bitmap->buffer[i] = pupdate_bitmap->swap_buffer[i];
                    pupdate_bitmap->swap_buffer[i] = NULL;
                }
                else {
                    /*
                     * current region has update data mark by Buffer field, need do merge.
                     * to do...
                     * use LONGLONG type maybe more efficient?
                     */
                    __u8* temp_buffer = pupdate_bitmap->buffer[i];
                    __u8* temp_swap_buffer = pupdate_bitmap->swap_buffer[i];
                    int j = 0;
                    for (j = 0; j < pupdate_bitmap->region_size; ++j, ++temp_buffer, ++temp_swap_buffer) {
                        if (*temp_buffer != 0) {
                            *temp_buffer = (*temp_buffer | *temp_swap_buffer);
                        }
                    }

                    /* zero swap_buffer after merge context to Buffer. */
                    memset(pupdate_bitmap->swap_buffer[i], 0, pupdate_bitmap->region_size);
                }
            }
        }
    }

    return 0;
}

static int 
seek_update_bitmap (pcdp_bitmap pupdate_bitmap, __u64* pdata_offset, __u32* pdata_length)
{
    __u64 pre_lcn = 0;

    if (NULL == pupdate_bitmap) {
        IO_ERROR("(%d) invalid parameters.\n", __LINE__);
        *pdata_length = 0;
        return -1;
    }

    pre_lcn = pupdate_bitmap->current_seek_lcn;

    while ( pupdate_bitmap->current_seek_region < pupdate_bitmap->region_count ) {
        if (NULL == pupdate_bitmap->swap_buffer[pupdate_bitmap->current_seek_region]) {
            /* current region has any data update */
            if ( pre_lcn != pupdate_bitmap->current_seek_lcn ) {
                /* some data have been copied */
                break;
            }
            else {
                pupdate_bitmap->current_seek_lcn = pre_lcn = 0;
                ++pupdate_bitmap->current_seek_region;
                continue;
            }
        }

        while ( pupdate_bitmap->current_seek_lcn < pupdate_bitmap->blocks_per_region ) {
            if ( ( (__u32*)pupdate_bitmap->swap_buffer[pupdate_bitmap->current_seek_region] )[pupdate_bitmap->current_seek_lcn / 32] & ( 1 << ( pupdate_bitmap->current_seek_lcn % 32 ) ) ) {
                if ( ( pupdate_bitmap->current_seek_lcn - pre_lcn + 1 ) * pupdate_bitmap->block_size <= MAX_READ_BLOCK_SIZE ) {
                    ++pupdate_bitmap->current_seek_lcn;
                }
                else {
                    /* cache swap_buffer is full, break */
                    break;
                }
            }
            else {
                if ( pre_lcn != pupdate_bitmap->current_seek_lcn ) {
                    /* some data have been copied */
                    break;
                }

                /* skip unused LCN */
                pre_lcn = ++pupdate_bitmap->current_seek_lcn;
            }
        }

        /* seek next region */
        if (pupdate_bitmap->current_seek_lcn == pre_lcn && ((++pupdate_bitmap->current_seek_region) < pupdate_bitmap->region_count)) {
            pupdate_bitmap->current_seek_lcn = pre_lcn = 0;
        }
        else {
            break;
        }
    }

    /* current offset(total) */
    *pdata_offset = (__u64)pupdate_bitmap->current_seek_region * pupdate_bitmap->blocks_per_region * pupdate_bitmap->block_size;
    *pdata_offset += pre_lcn * pupdate_bitmap->block_size;

    /* current read swap_buffer length(total) */
    *pdata_length = (__u32)((pupdate_bitmap->current_seek_lcn - pre_lcn) * pupdate_bitmap->block_size);

    if ((pre_lcn == pupdate_bitmap->current_seek_lcn) || (pre_lcn != 0 && (*pdata_offset == 0))) {
        /* all data has been copied */
        *pdata_length = 0;
        pupdate_bitmap->current_seek_lcn = 0;
        pupdate_bitmap->current_seek_region = 0;
    }

    return 0;
}

static void 
clear_update_bitmap (pcdp_bitmap pupdate_bitmap)
{
    if (NULL != pupdate_bitmap) {
        /* free bitmap buffer */
        if (NULL != pupdate_bitmap->buffer) {
            uint i;
            for (i = 0; i < pupdate_bitmap->region_count; ++i) {
                if (NULL != pupdate_bitmap->buffer[i]) {
                    kfree(pupdate_bitmap->buffer[i]);
                    pupdate_bitmap->buffer[i] = NULL;
                }
            }

            kfree(pupdate_bitmap->buffer);
            pupdate_bitmap->buffer = NULL;
        }

        /* free bitmap swap buffer */
        if (NULL != pupdate_bitmap->swap_buffer) {
            uint i;
            for (i = 0; i < pupdate_bitmap->region_count; ++i) {
                if (NULL != pupdate_bitmap->swap_buffer[i]) {
                    kfree(pupdate_bitmap->swap_buffer[i]);
                    pupdate_bitmap->swap_buffer[i] = NULL;
                }
            }

            kfree(pupdate_bitmap->swap_buffer);
            pupdate_bitmap->swap_buffer = NULL;
        }

        kfree(pupdate_bitmap);
        pupdate_bitmap = NULL;
    }
}

static void
reinit_seek_info_bitmap(pcdp_bitmap pupdate_bitmap)
{
    if (NULL != pupdate_bitmap) {
        pupdate_bitmap->current_seek_lcn = 0;
        pupdate_bitmap->current_seek_region = 0;
    }
}

static int 
packed_logitem_in_fixed_length (struct cdp_filter_task_info *task_info, struct cdp_filter_data_info *data_info)
{
    /*
     * packed log item to swap buffer with fixed length, 
     * it's not a good idea, for beside compatibility previous I/O log file format for upper layer.
     */
    if (data_info->residue_io_data_len > MAX_LOG_ITEM_SIZE) {
        /* current I/O has been sliced */
        data_info->residue_io_data_len -= MAX_LOG_ITEM_SIZE;
        data_info->item_head.data_length = MAX_LOG_ITEM_SIZE;
    }
    else {
        data_info->item_head.data_length = data_info->residue_io_data_len;
        data_info->residue_io_data_len = 0;
    }

    /* copy log head to swap_buffer */
    memcpy (task_info->swap_buffer + task_info->swap_buffer_offset, &data_info->item_head, sizeof(struct cdp_filter_io_pack));
    task_info->swap_buffer_offset += LOG_HEAD_SIZE;

    /* copy data packet to swap_buffer */
    memcpy (task_info->swap_buffer + task_info->swap_buffer_offset, data_info->data + data_info->io_data_buffer_offset, data_info->item_head.data_length);
    task_info->swap_buffer_offset += MAX_LOG_ITEM_SIZE;

    if (0 != data_info->residue_io_data_len) {
        /* current data_info has not been finished yet */
        data_info->io_data_buffer_offset += data_info->item_head.data_length;
        data_info->item_head.data_offset += data_info->item_head.data_length;
    }

    return 0;
}

void 
task_list_init(void)
{
    __task_cnt = (MAX_TASK_NUM);
    IO_DEBUG("(%d) task list init: %d.\n", __LINE__, __task_cnt);
    memset(__task_bitmap, 0, sizeof(__task_bitmap));
    while ( __task_cnt ) 
        __task_list[--__task_cnt] = NULL;
}

void 
dev_list_init(void)
{
    int dev_cnt = (MAX_DEV_NUM);
    IO_DEBUG("(%d) dev list init: %d.\n", __LINE__, dev_cnt);
    while ( dev_cnt )
        __dev_list[--dev_cnt] = NULL;
}

/**
 * get the first zero bit
 * returns 0 presents fail to get a usable zero bit.
 */
int 
cdp_filter_get_task_slot(void)
{
    int i = 0;
    if ( (MAX_TASK_NUM) < __task_cnt ) 
        return -1;

    /* get the first usable slot number */
    for ( i = 0; i < (TASK_MAP_LEN)*(BITS_PER_LONG); ++i ) {
        /* former value equal to 0 indicates it's available */
        if ( 0 == test_and_set_bit(i, __task_bitmap) ) {
            ++__task_cnt;
            return i;
        }
    }

    return -1;
}

void 
cdp_filter_clear_task_slot(int slot)
{
    IO_MSG("(%d) cdp_filter_clear_task_slot %d.\n", __LINE__, slot);
    if ( slot < 0 || (MAX_TASK_NUM) <= slot ) {
        IO_ERROR("(%d) slot out of limit, current slot is: %d.\n", __LINE__, slot);
        return;
    }

    if (0 != test_and_clear_bit(slot, __task_bitmap)) {
        IO_MSG("(%d) slot %d clear %d.\n", __LINE__, slot, __task_cnt);
        --__task_cnt;
    } else {
        IO_WARN("(%d) slot %d was already cleared %d.\n", __LINE__, slot, __task_cnt);
    }
}

int 
cdp_filter_test_task_slot(int slot)
{
    if ( slot < 0 || (MAX_TASK_NUM) <= slot ) {
        IO_ERROR("(%d) slot out of limit, current slot is: %d.\n", __LINE__, slot);
        return 1;
    }

    /* return current value of this bit point to */
    return test_bit(slot, __task_bitmap);
}

int 
cdp_filter_calc_dev_slot(int major, int minor)
{
    int i = 0;
    int ret = -1;

    for (; i < (MAX_DEV_NUM); ++i) {
       if ( __dev_list [i] 
            && __dev_list [i]->major == major 
            && __dev_list [i]->minor == minor) {
            ret = i;
            break;
        }
    }

    return ret;
}

int 
cdp_filter_get_available_dev_slot(void)
{
    int i = 0;
    int ret = -1;
    for (; i < (MAX_DEV_NUM); ++i) {
        if (__dev_list [i] == NULL) {
            ret = i;
            break;
        }
    }

    return ret;
}

void 
cdp_filter_clear_device_list(void)
{
    int i;
    IO_MSG("(%d) clear device list begin.\n", __LINE__);

    for ( i = 0; i < (MAX_DEV_NUM); ++i ) {
        /*
         * restore make_request_fn and free device list.
         */
        if ( NULL != __dev_list[i]) {
            __dev_list[i]->flag = CDP_DISK_NON_MONITOR;

            IO_MSG("(%d) __dev_list[i]->disk==null: %d i: %d devName: %s.\n", 
                   __LINE__, __dev_list[i]->disk == NULL, i, __dev_list[i]->dev_name);

            if(__dev_list[i]->disk
                && __dev_list[i]->disk->queue
                && __dev_list[i]->disk->queue->make_request_fn
                && __dev_list[i]->make_request_fn) {
                __dev_list[i]->disk->queue->make_request_fn = __dev_list[i]->make_request_fn;
            }
            else {
                if (__dev_list[i]->disk) {
                    IO_ERROR("(%d) set make_request_fn error.\n", __LINE__);
                }
            }

            kfree(__dev_list[i]);
            __dev_list[i] = NULL;
        }
    }

    IO_MSG("(%d) clear device list end.\n", __LINE__);
}

/**
 * cdp_filter_clear_task_by_id - clear the resource of specified task.
 * @task_id: the id of witch task need to be clear.
 */
void
cdp_filter_clear_task_by_id(uint task_id)
{
    IO_MSG("(%d) clear task by id begin.\n", __LINE__);

    if ( ((MAX_TASK_NUM) > task_id) && __task_list[task_id] ) {
        /*detect io thread, if being alive, stop it*/
        if ( NULL != __task_list[task_id]->io_thread && __task_list[task_id]->thread_state ) {
            /*reset @thread_state prior to stopping thread to stop receiving bio*/
            __task_list[task_id]->thread_state = 0;
            /*wake up the thread*/
            IO_MSG("(%d) prepare to stop thread <task id: %d>.\n", __LINE__, task_id);
            up(&__task_list[task_id]->io_list_sem);
            /*set kthread->should_stop = 1 and wait until thread exits*/
            if(!IS_ERR(__task_list[task_id]->io_thread)) {
                kthread_stop(__task_list[task_id]->io_thread);
            }
            __task_list[task_id]->io_thread = NULL;
            IO_MSG("(%d) succeed to stop io_thread <task id: %d>.\n", __LINE__, task_id);
        }

        /*detect notify thread, if being alive, stop it*/
        if (__task_list[task_id]->notify_thread != NULL) {
            if(!IS_ERR(__task_list[task_id]->notify_thread)) {
                kthread_stop(__task_list[task_id]->notify_thread);
            }
            __task_list[task_id]->notify_thread = NULL;
            IO_MSG("(%d) succeed to stop notify_thread <task id: %d>.\n", __LINE__, task_id);
        }

        /* notify app caller */
        kernel_netlink_data_notify(__task_list[task_id]);
        kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_id, __task_list[task_id]->pid, 0);

        IO_MSG("(%d) succeed to send data notification to client <task id: %d>.\n", __LINE__, task_id);
    }
    else {
        IO_ERROR("(%d) stop monitor and notify thread error, task id is: %d.\n", __LINE__, task_id);
    }
    IO_MSG("(%d) clear task by id end.\n", __LINE__);
}

void 
cdp_filter_del_tmp_io_list(struct list_head *tmp_io_list)
{
    struct cdp_filter_data_info *pos;
    struct cdp_filter_data_info *item;

    IO_MSG("(%d) delete tmp io list begin.\n", __LINE__);

    pos = NULL;
    item = NULL;
    if ( NULL != tmp_io_list ) {
        /* release tmp list io data packages */
        list_for_each_entry_safe(pos, item, tmp_io_list, list) {
            IO_DEBUG("(%d) delete tmp io: %p.\n", __LINE__, &pos->list);
            list_del(&pos->list);
            IO_DEBUG("(%d) kfree tmp pos: %p.\n", __LINE__, pos);

            if (pos->data != NULL) {
                pos->item_head.data_length <= PAGE_SIZE
                    ? kfree(pos->data)
                    : free_pages((unsigned long)pos->data, get_order(pos->item_head.data_length));
                pos->data = NULL;
            }
            kfree(pos);
            pos = NULL;
        }
    }

    IO_MSG("(%d) delete tmp io list end.\n", __LINE__);
}

void 
cdp_filter_del_io_list(struct cdp_filter_task_info *task_info)
{
    struct cdp_filter_data_info *pos;
    struct cdp_filter_data_info *item;

    IO_MSG("(%d) delete io list begin.\n", __LINE__);

    pos = NULL;
    item = NULL;
    if ( NULL != task_info ) {
        /* release all uncommitted io data packages */
        list_for_each_entry_safe(pos, item, &task_info->io_list, list) {
            IO_DEBUG("(%d) delete io: %p.\n", __LINE__, &pos->list);
            list_del(&pos->list);
            IO_DEBUG("(%d) kfree pos: %p.\n", __LINE__, pos);

            if (pos->data != NULL) {
                pos->item_head.data_length <= PAGE_SIZE
                    ? kfree(pos->data)
                    : free_pages((unsigned long)pos->data, get_order(pos->item_head.data_length));
                pos->data = NULL;
            }
            kfree(pos);
            pos = NULL;
        }
    }

    IO_MSG("(%d) delete io list end.\n", __LINE__);
}

void 
cdp_filter_del_sid_list(struct cdp_filter_task_info *task_info)
{
    struct cdp_filter_sid *pos;
    struct cdp_filter_sid *item;

    IO_MSG("(%d) delete sid list begin.\n", __LINE__);

    pos = NULL;
    item = NULL;
    if ( NULL != task_info ) {
        /* release all uncommitted sids */
        list_for_each_entry_safe(pos, item, &task_info->sid_list, list) {
            list_del(&pos->list);
            kfree(pos);
            pos = NULL;
        }
    }

    IO_MSG("(%d) delete sid list end.\n", __LINE__);
}

/**
 * delete task_id specified task from __task_list
 */
void 
cdp_filter_del_task_by_id(uint task_id)
{
    IO_MSG("(%d) delete task by id begin.\n", __LINE__);

    if ( ((MAX_TASK_NUM) > task_id) && __task_list[task_id] ) {
        cdp_filter_del_io_list(__task_list[task_id]);
        cdp_filter_del_sid_list(__task_list[task_id]);

        if ( NULL != __task_list[task_id]->log_file_handle ) {
            filp_close(__task_list[task_id]->log_file_handle, NULL);
            __task_list[task_id]->log_file_handle = NULL;
        }

        if (__task_list[task_id]->log_file_head != NULL) {
            kfree(__task_list[task_id]->log_file_head);
            __task_list[task_id]->log_file_head = NULL;
        }

        if (__task_list[task_id]->monitor_info != NULL) {
            if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
                /* release update bitmap */
                unsigned int i;
                for (i = 0; i < __task_list[task_id]->monitor_infos; ++i) {
                    clear_update_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
                }
            }

            kfree(__task_list[task_id]->monitor_info);
            __task_list[task_id]->monitor_info = NULL;
        }

        if (CDP_NO_CACHED_MODE == __task_list[task_id]->work_mode) {
            if (NULL == __task_list[task_id]->swap_buffer) {
                vfree(__task_list[task_id]->swap_buffer);
                __task_list[task_id]->swap_buffer = NULL;
            }
        }

        if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
            if (NULL == __task_list[task_id]->io_metadata_entries) {
                kfree(__task_list[task_id]->io_metadata_entries);
                __task_list[task_id]->io_metadata_entries = NULL;
            }
        }

        if (__task_list[task_id] != NULL) {
            kfree(__task_list[task_id]);
            __task_list[task_id] = NULL;
        }

        cdp_filter_clear_task_slot(task_id);
    }
    IO_MSG("(%d) delete task by id end.\n", __LINE__);
}

bool 
cdp_filter_task_is_alive(struct cdp_filter_task_info *task_info)
{
    struct task_struct *task = NULL;
    int ret = 0;
    __u32 pid = task_info->pid;

    if ( (CDP_NO_CACHED_MODE == task_info->work_mode) 
        || (CDP_BITMAP_MODE == task_info->work_mode) ) {
        return 1;
    }

    // cdp has no upper layer and default process exists
	return 1;

#if defined(k_4_x_x) || defined(k_3_10_0) || defined(k_2_6_32) || defined(k_2_6_26) || defined(k_2_6_24)
    // to do...
#else
    read_lock_irq(&tasklist_lock);
#endif

#if defined(k_4_x_x) || defined(k_3_10_0) || defined(k_2_6_32)
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
#else
    task = find_task_by_pid(pid);
#endif

    if ( NULL != task ) {
        ret = 1;
    }

#if defined(k_4_x_x) || defined(k_3_10_0) || defined(k_2_6_32) || defined(k_2_6_26) || defined(k_2_6_24)
    // to do...
#else
    read_unlock_irq(&tasklist_lock);
#endif

    IO_DEBUG("(%d) io filter task is alive: %d.\n", __LINE__, ret);
    return ret;
}

void 
cdp_filter_clear_zombie_task(void)
{
    int i = 0;

    /*
     * clean up zombie task.
     * notes: zombie task usual case by upper layer abnormal termination.
     * must do step as follows: 
     * 1.clear device list and restore original make_request_fn;
     * 2.stop specified id task work threads;
     * 3.clear specified id task from task list and release resource;
     * 4.clear task slot;
     */
    for ( i = 0; i < (MAX_TASK_NUM); ++i ) {
        if ( __task_list[i] && (CDP_CACHED_MODE == __task_list[i]->work_mode) ) {
            IO_ERROR("(%d) clean up zombie task, task_id: %d, pid: %d.\n", __LINE__, i, __task_list[i]->pid);
            cdp_filter_clear_device_list();
            cdp_filter_clear_task_by_id(__task_list[i]->task_id);
            cdp_filter_del_task_by_id(__task_list[i]->task_id);
            cdp_filter_clear_task_slot(i);
        }
    }
}

int 
cdp_filter_seek_task_id(struct cdp_filter_init_request *init_request)
{
    int i = 0, j = 0;
    int task_id = -1;
    int minor_no = -1;
    bool is_same_data_source = true;

    if ( NULL == init_request ) {
        IO_ERROR("(%d) init task param is null.\n", __LINE__);
        return -1;
    }

    if ( 0 == __task_cnt ) {
        /* there is any active task */
        task_id = cdp_filter_get_task_slot();
        return task_id;
    }
    else {
        /* find active task */
        for ( i = 0; i < (MAX_TASK_NUM); ++i ) {
            if ( __task_list[i] && (CDP_NO_CACHED_MODE == __task_list[i]->work_mode
                                    || CDP_BITMAP_MODE == __task_list[i]->work_mode) ) {
                task_id = i;
                IO_MSG("(%d) find active task id %d.\n", __LINE__, task_id);
                break;
            }
        }

        if (-1 == task_id) {
            IO_ERROR("(%d) exist other work mode task already.\n", __LINE__);
            goto seek_task_id_exit;
        }

        if ( __task_list[task_id]->monitor_infos == init_request->item_cnt ) {
            for ( j = 0; j < init_request->item_cnt; ++j ) {
                minor_no = cdp_filter_calc_dev_slot(init_request->init_info[j].major_no, init_request->init_info[j].minor_no);
                if ( minor_no >= 0 && NULL != __dev_list[minor_no] && (CDP_DISK_IN_MONITOR) == __dev_list[minor_no]->flag ) {
                    continue;
                }
                else {
                    is_same_data_source = false;
                    break;
                }
            }

            if (is_same_data_source) {
                /* update task parameters */
                IO_MSG("(%d) current task has same data source with exist active task %d.\n", __LINE__, task_id);
            }
            else {
                /* update data source if necessary */
                /* to do... */
                IO_MSG("(%d) current task has different data source with exist active task %d.\n", __LINE__, task_id);
                task_id = -1;
                goto seek_task_id_exit;
            }
        }
        else {
            /* update data source if necessary */
            /* to do... */
            IO_MSG("(%d) current task has different monitored infos with exist active task %d.\n", __LINE__, task_id);
            task_id = -1;
            goto seek_task_id_exit;
        }
    }

seek_task_id_exit:
    return task_id;
}

long 
cdp_filter_create_log_file(struct cdp_filter_task_info *task_info, __u64 timestamp)
{
    long err_no = 0;

    if ( NULL == task_info ) 
        return -EINVAL;

    memset(task_info->log_file_name, 0, DISK_DOS_NAME_LEN);

    /*format of log_file_name : log_path/timestamp_task_id_pid_log_id, EG: /log/.CDPLOG/.1481075498000000_0_100_0*/
    snprintf(task_info->log_file_name, DISK_DOS_NAME_LEN-1, "%s/.%lld_%d_%d_%lld", 
             task_info->log_path, timestamp, task_info->task_id, task_info->pid, ++task_info->log_id);

    IO_MSG("(%d) create log file %s  \n", __LINE__, task_info->log_file_name);

    task_info->log_file_handle = filp_open(task_info->log_file_name, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG);
    if ( IS_ERR(task_info->log_file_handle) ) {
        IO_ERROR("(%d) failed to creat log file %s, the ERROR CODE is: %ld.\n",
                 __LINE__, task_info->log_file_name, err_no = PTR_ERR(task_info->log_file_handle));
        return err_no;
    }

    vfs_llseek(task_info->log_file_handle, MAX_SECTION_SIZE, SEEK_SET);
    task_info->offset = (LOG_FILE_HEAD_SIZE);
    task_info->item_cnt = 0;
    task_info->item_cnt_old = 0;
    return err_no;
}

/**
 * Try to get the first valid major device number, algorithm is illustrated as follows:
 * 1. get the first valid DEV_MAJOR_NUM_STR stated device and assign value to tmp
 * 2. get the first valid MD_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp
 * 3. get the first valid MDP_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp 
 * 4. get the first valid LVM_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp
 */
char *
cdp_filter_get_first_devno(char* buf )
{
    char *tmp = NULL;
    char *tmp1 = NULL;

    if (buf == NULL) 
        return tmp;

    /* get the first valid DEV_MAJOR_NUM_STR stated device and assign value to tmp*/
    if ( NULL != (tmp1 = strstr(buf, DEV_MAJOR_NUM_STR)) ) {
        tmp = tmp1;
    }

    /*get the first valid MD_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp*/
    if ( NULL != (tmp1 = strstr(buf, MD_MAJOR_NUM_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    /*get the first valid MDP_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp*/
    if ( NULL != (tmp1 = strstr(buf, MDP_MAJOR_NUM_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    /*get the first valid LVM_MAJOR_NUM_STR stated device and compare with former first device, assign the earlier device to tmp*/
    if ( NULL != (tmp1 = strstr(buf, LVM_MAJOR_NUM_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM65_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM66_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM67_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM68_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM69_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM70_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM71_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM136_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM137_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM138_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM139_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM140_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM141_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM142_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM143_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM252_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    if ( NULL != (tmp1 = strstr(buf, DEV_SCSI_MAJOR_NUM202_STR)) ) {
        if ( NULL == tmp )
            tmp = tmp1;
        else 
            if ( tmp1 < tmp )
                tmp = tmp1;
    }

    return tmp;
}

int 
cdp_filter_update_device_list(int dev_num, struct cdp_filter_init_info *init_info)
{
    int ret;
    int slot = -1;
    int major = 0;
    int minor = 0;
    int err_no = 0;
    char *buf = NULL;
    char *buf_tmp = NULL;
    char *tmp = NULL;
    char *token = NULL;
    struct file *fd = NULL;
    struct block_device *dev = NULL;
#if defined(k_4_x_x)
    loff_t pos = 0;
#endif

    if (init_info == NULL) {
        IO_ERROR("(%d) update device list parameter is null.\n", __LINE__);
        goto update_dev_exit;
    }

    fd = filp_open("/proc/partitions", O_RDONLY, 0);
    if ( IS_ERR(fd) ) {
        IO_ERROR("(%d) fail to open file</proc/partitions>, the ERROR CODE is: %d.\n",
                 __LINE__, err_no = PTR_ERR(fd));
        goto update_dev_exit;
    }

    buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if ( IS_ERR(buf) ) {
        IO_ERROR("(%d) update device list failed to allocate memory, the ERROR CODE is: %d.\n", 
                 __LINE__, err_no = PTR_ERR(buf));
        goto update_dev_exit;
    }
    memset(buf, 0, PAGE_SIZE);

#if defined(k_4_x_x)
    ret = kernel_read(fd, buf, PAGE_SIZE, &pos);
#else
    ret = kernel_read(fd, 0, buf, PAGE_SIZE);
#endif
    if ( 0 == ret ) {
        IO_ERROR("(%d) failed to read data from file</proc/partitions>, the ERROR CODE is: %d.\n",
                 __LINE__, err_no = -EPERM);
        goto update_dev_exit;
    }

    buf_tmp = buf;
    while ( NULL != (tmp = cdp_filter_get_first_devno(buf)) ) {
        /*major device number*/
        while ( (token = strsep(&tmp, " ")) && !ISDIGIT(*token) );
        major = simple_strtol(token, NULL, 10);
        IO_DEBUG("(%d) major device number is: %d.\n", __LINE__, major);

        if ( (DEV_MAJOR_NUM) == major || (MD_MAJOR_NUM) == major 
            || (LVM_MAJOR_NUM) == major || (MDP_MAJOR_NUM) == major 
            || (DEV_SCSI_MAJOR_NUM65) == major || (DEV_SCSI_MAJOR_NUM66) == major
            || (DEV_SCSI_MAJOR_NUM67) == major || (DEV_SCSI_MAJOR_NUM68) == major
            || (DEV_SCSI_MAJOR_NUM69) == major || (DEV_SCSI_MAJOR_NUM70) == major
            || (DEV_SCSI_MAJOR_NUM71) == major || (DEV_SCSI_MAJOR_NUM136) == major
            || (DEV_SCSI_MAJOR_NUM137) == major || (DEV_SCSI_MAJOR_NUM138) == major
            || (DEV_SCSI_MAJOR_NUM139) == major || (DEV_SCSI_MAJOR_NUM140) == major
            || (DEV_SCSI_MAJOR_NUM141) == major || (DEV_SCSI_MAJOR_NUM142) == major
            || (DEV_SCSI_MAJOR_NUM143) == major || (DEV_SCSI_MAJOR_NUM252) == major
            || (DEV_SCSI_MAJOR_NUM202) == major) {
            /*minor device number*/
            while ( (token = strsep(&tmp, " ")) && !ISDIGIT(*token) );
            minor = simple_strtol(token, NULL, 10);
            IO_DEBUG("(%d) minor device number is: %d.\n", __LINE__, minor);

            /*skip device size*/
            while ( (token = strsep(&tmp, " ")) && !ISDIGIT(*token) );

            /*device name*/
            token = strsep(&tmp, ".\n");
            IO_DEBUG("(%d) device name is: %s.\n", __LINE__, token);

            if ((slot = cdp_filter_calc_dev_slot (major, minor)) > 0) {
                IO_ERROR("(%d) device is already exists error %d.\n", __LINE__, slot);
                goto update_next;
            }

            if ( (slot = cdp_filter_get_available_dev_slot()) < 0 ) {
                IO_ERROR("(%d) Err slot:%d\n", __LINE__, slot);
                err_no = slot;
                goto update_dev_exit;
            }
            IO_DEBUG("(%d) desired slot is: %d.\n", __LINE__, slot);

            dev = bdget(MKDEV(major, minor));
            if ( IS_ERR(dev) ) {
                IO_ERROR("(%d) fail to get block_device for device<%d, %d>, the ERROR CODE is: %d.\n", 
                         __LINE__, major, minor, err_no = PTR_ERR(dev));
                goto update_dev_exit;
            }

            __dev_list[slot] = kmalloc(sizeof(struct cdp_filter_dev_info), GFP_KERNEL);
            if ( !__dev_list[slot] && IS_ERR(__dev_list[slot]) ) {
                IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %d. %d.\n", 
                         __LINE__, err_no = PTR_ERR(__dev_list[slot]), __dev_list[slot] == NULL);
                goto update_dev_exit;
            }

            /*
             * fill up dev_info.
             */
            __dev_list[slot]->major = major;
            __dev_list[slot]->minor = minor;
            __dev_list[slot]->is_disk = CDP_IS_PART;
            __dev_list[slot]->dev_no = dev->bd_dev;
            __dev_list[slot]->flag = CDP_DISK_NON_MONITOR;
            /*disk device and partitions in this disk use the same QUEUE and MAKE_REQUEST_FN*/
            if ( dev->bd_disk ) {
                __dev_list[slot]->queue = dev->bd_disk->queue;
                __dev_list[slot]->make_request_fn = dev->bd_disk->queue->make_request_fn;
            }

            strcpy(__dev_list[slot]->dev_name, "/dev/");
            strncat(__dev_list[slot]->dev_name, token, DISK_DOS_NAME_LEN-5);
            __dev_list[slot]->dev_name[DISK_DOS_NAME_LEN-1] = '\0';
            __dev_list[slot]->disk = dev->bd_disk;
            __dev_list[slot]->part = dev->bd_part;
            if ( dev == dev->bd_contains ) {
                __dev_list[slot]->is_disk = CDP_IS_DISK;
            }

            IO_DEBUG("(%d) dev info<dev_no: %d, dev_name: %s>. is_disk:%d, disk is null :%d, part is null :%d disk is null:%d solt:%d\n", 
                     __LINE__, __dev_list[slot]->dev_no, __dev_list[slot]->dev_name, 
                     __dev_list[slot]->is_disk, dev->bd_disk == NULL, dev->bd_part == NULL, 
                     __dev_list[slot]->disk == NULL, slot);
        }

update_next:
        /*next line*/
        buf = tmp;
    }

update_dev_exit:
    if ( NULL != fd ) {
        filp_close(fd, NULL);
        fd = NULL;
    }

    if ( NULL != buf_tmp ) {
        kfree(buf_tmp);
        buf_tmp = NULL;
    }

    return err_no;
}

void 
cdp_filter_stop_task_thread(void)
{
    uint i = 0;
    IO_MSG("(%d) stop task worker threads.\n", __LINE__);

    for ( i = 0; i < (MAX_TASK_NUM); ++i ) {
        if ( NULL != __task_list[i] && NULL != __task_list[i]->io_thread && __task_list[i]->thread_state ) {
            /*reset @thread_state prior to stopping thread to stop receiving bio*/
            __task_list[i]->thread_state = 0;

            /*wake up the thread*/
            up(&__task_list[i]->io_list_sem);
            if(!IS_ERR(__task_list[i]->io_thread)) {
                IO_MSG("(%d) start to stop io thread: %d.\n", __LINE__, __task_list[i]->io_thread->pid);
                kthread_stop(__task_list[i]->io_thread);
            }
            __task_list[i]->io_thread = NULL;
            IO_MSG("(%d) succeed to stop io_thread <task id: %d>.\n", __LINE__, i);
        }

        if (NULL != __task_list[i] && __task_list[i]->notify_thread != NULL) {
            if(!IS_ERR(__task_list[i]->notify_thread)) {
                IO_MSG("(%d) start to stop notify thread: %d.\n", __LINE__, __task_list[i]->notify_thread->pid);
                kthread_stop(__task_list[i]->notify_thread);
            }
            __task_list[i]->notify_thread = NULL;
            IO_MSG("(%d) succeed to stop notify_thread <task id: %d>.\n", __LINE__, i);
        }
    }

    IO_MSG("(%d) stop task worker threads successfully.\n", __LINE__);
}

/**
 * exit the drive and clear up __task_list
 */
void 
cdp_filter_del_task_list(void)
{
    uint i;
    cdp_filter_clear_device_list();
    cdp_filter_stop_task_thread();

    IO_MSG("(%d) delete task list.\n", __LINE__);

    for ( i = 0; i < (MAX_TASK_NUM); ++i ) {
        if ( NULL != __task_list[i] ) {
            cdp_filter_del_io_list(__task_list[i]);
            cdp_filter_del_sid_list(__task_list[i]);

            if ( NULL != __task_list[i]->log_file_handle ) {
                filp_close(__task_list[i]->log_file_handle, NULL);
                __task_list[i]->log_file_handle = NULL;
            }

            if (__task_list[i]->log_file_head != NULL) {
                kfree(__task_list[i]->log_file_head);
                __task_list[i]->log_file_head = NULL;
            }

            if (__task_list[i]->monitor_info != NULL) {
                kfree(__task_list[i]->monitor_info);
                __task_list[i]->monitor_info = NULL;
            }

            if (__task_list[i]) {
                kfree(__task_list[i]);
                __task_list[i] = NULL;
            }

            cdp_filter_clear_task_slot(i);
        }
    }

    IO_MSG("(%d) delete task list successfully.\n", __LINE__);
}

struct sk_buff* 
cdp_filter_sk_buff_create(__u32 type, __u32 taskid, __u32 pid, __s32 errno)
{
    struct sk_buff *skb;
    struct nlmsghdr *hdr;
    struct cdp_filter_response *resp;

#if defined(k_4_x_x) || (defined(k_3_10_0) || defined(k_2_6_24) || defined(k_2_6_32)) && BITS_PER_LONG > 32
    unsigned int old_tail;
#else
    unsigned char *old_tail;
#endif

    /*
     * NLMSG_SPACE(sizeof(struct cdp_filter_response)) equal to 
     * sizof(struct nlmsghdr) + sizeof(struct cdp_filter_response) align with 4U.
     */
    int size = NLMSG_SPACE(sizeof(struct cdp_filter_response));
    if ( NULL == (skb = alloc_skb(size, GFP_KERNEL)) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", 
                 __LINE__, PTR_ERR(skb));
        return NULL;
    }

    old_tail = skb->tail;

#if defined(k_2_6_9) || defined(k_2_6_13)
    NETLINK_CB(skb).dst_groups = 0;
#else
    NETLINK_CB(skb).dst_group = 0;
#endif

    /*
     * data buffer is insufficient.
     */
    if ( skb_tailroom(skb) < (int)NLMSG_SPACE(size - sizeof(*hdr)) ) {
        IO_ERROR("(%d) data buffer is insufficient.\n", __LINE__);
        return NULL;
    }

    /*
     * fill nlmsghdr and stretch data buffer.
     */
#if defined(k_2_6_9)
    hdr = __nlmsg_put(skb, pid, 0, type, size-sizeof(*hdr));
#else
    hdr = __nlmsg_put(skb, pid, 0, type, size-sizeof(*hdr), 0);
#endif

    /*
     * set nlmsg_len.
     */
    hdr->nlmsg_len = skb->tail - old_tail;

    /*
     * fill data buffer.
     */
    resp = (struct cdp_filter_response*)(char*)NLMSG_DATA(hdr);
    resp->task_id = taskid;
    resp->error_no = errno;
    return skb;
}

struct sk_buff* 
cdp_filter_bitmap_sk_buff_create(__u32 type, __u32 taskid, __u32 pid, __s32 errno)
{
    struct sk_buff *skb = NULL;
    struct nlmsghdr *hdr;
    struct cdp_filter_bitmap_response *bitmap_response;
#if defined(k_4_x_x) || (defined(k_3_10_0) || defined(k_2_6_24) || defined(k_2_6_32)) && BITS_PER_LONG > 32
    unsigned int old_tail;
#else
    unsigned char *old_tail;
#endif
    int size;

    if ( NULL == __task_list[taskid] ) {
        IO_ERROR("(%d) parameter is invalid.\n", __LINE__);
        return NULL;
    }

    size = NLMSG_SPACE(sizeof(struct cdp_filter_bitmap_response));
    if ( NULL == (skb = alloc_skb(size, GFP_KERNEL)) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(skb));
        return NULL;
    }

    old_tail = skb->tail;

    /* disable broadcast */
#if defined(k_2_6_9) || defined(k_2_6_13)
    NETLINK_CB(skb).dst_groups = 0;
#else
    NETLINK_CB(skb).dst_group = 0;
#endif

    /* data buffer is insufficient */
    if ( skb_tailroom(skb) < (int)NLMSG_SPACE(size - sizeof(*hdr)) ) {
        IO_ERROR("(%d) data buffer is insufficient.\n", __LINE__);
        return NULL;
    }

    /* fill nlmsghdr and stretch data buffer */
#if defined(k_2_6_9)
    hdr = __nlmsg_put(skb, pid, 0, type, size-sizeof(*hdr));
#else
    hdr = __nlmsg_put(skb, pid, 0, type, size-sizeof(*hdr), 0);
#endif

    /* set nlmsg_len */
    hdr->nlmsg_len = skb->tail - old_tail;

    /* fill data buffer */
    bitmap_response = (struct cdp_filter_bitmap_response*)(char*)NLMSG_DATA(hdr);
    bitmap_response->task_id = taskid;
    memcpy((char*)bitmap_response->io_metadata_entries, (const char*)__task_list[taskid]->io_metadata_entries, sizeof(struct cdp_filter_io_metadata)*MAX_IO_METADATA_ENTRIES);

    return skb;
}

void 
kernel_netlink_response(struct sock *sock, __u32 type, __u32 taskid, __u32 pid, __s32 errno)
{
    int ret = -1;
    struct sk_buff *skb;

    IO_DEBUG("(%d) Prepare to send netlink response <type %d> to task <id %d>. errno %d.\n", 
             __LINE__, type, taskid, errno);
    IO_MSG("(%d) Prepare to send netlink response <type %d> to task <id %d>. errno %d.\n", 
             __LINE__, type, taskid, errno);

    if (sock == NULL) {
        IO_ERROR("(%d) netlink response sock is null error <type %d> to task <id %d>.\n", 
                 __LINE__, type, taskid);
        IO_MSG("(%d) netlink response sock is null error <type %d> to task <id %d>.\n", 
                 __LINE__, type, taskid);
        return;
    }

    switch ( type ) {
        case CDP_CMD_INIT:          /*0x00000001*/
        case CDP_CMD_REINIT:        /*0x00000007*/
        case CDP_CMD_STARTUP:       /*0x00000002*/
        case CDP_CMD_RESTART:       /*0x00000008*/
        case CDP_CMD_DEL:           /*0x00000004*/
        case CDP_CMD_STOP:          /*0x00000005*/
        case CDP_CMD_CONSIS:        /*0x0000000A*/
        case CDP_CMD_EXCP:          /*0x00000009*/
        case CDP_CMD_SET_BITMAP:    /*0x00000010*/
        case CDP_CMD_RESET_BITMAP:  /*0x00000011*/
        case CDP_CMD_MERGE_BITMAP:  /*0x00000012*/
        case CDP_CMD_STATUS:        /*0x00000014*/
            {
                skb = cdp_filter_sk_buff_create(type, taskid, pid, errno);
				IO_MSG("(%d) pid %d\n", __LINE__, pid);
                if ( NULL != skb ) {
                    /* set nonblock to 0 mean block until has available receive buffer */
                    ret = netlink_unicast(sock, skb, pid, 0);
                }

                if ( ret < 0 ) {
                    IO_ERROR("(%d) failed to send netlink response <type %d> to task <id %d>, return code is: %d.\n ",
                             __LINE__, type, taskid, ret);
                }
                else {
                    IO_DEBUG("(%d) succeed to send netlink response <type %d> to task <id %d>.\n", 
                             __LINE__, type, taskid);
                }
            }
            break;
        case CDP_CMD_GET_BITMAP:    /*0x00000013*/
            {
                skb = cdp_filter_bitmap_sk_buff_create(type, taskid, pid, errno);
                if ( NULL != skb ) {
                    /* set nonblock to 0 mean block until has available receive buffer */
                    ret = netlink_unicast(sock, skb, pid, 0);
                }

                if ( ret < 0 ) {
                    IO_ERROR("(%d) failed to send netlink response <type %d> to task <id %d>, return code is: %d.\n ",
                             __LINE__, type, taskid, ret);
                }
                else {
                    IO_DEBUG("(%d) succeed to send netlink response <type %d> to task <id %d>.\n", 
                             __LINE__, type, taskid);
                }
            }
            break;
        default:
            IO_WARN("(%d) failed to recognize this respond request, received respond id is: %d.\n",
                    __LINE__, type);
            break;
    }
}

static void 
init_cdp_task(struct cdp_filter_init_request* init_request, struct nlmsghdr *hdr)
{
    long int err_code;
    int i;
    int task_id;
    // struct timeval tv;
    err_code = 0;
    task_id = -1;
    __logitemcount = 0;

    if (__netlink_sk == NULL) {
        IO_ERROR("(%d) init task param __netlink_sk is null.\n", __LINE__);
        return;
    }

    if (init_request == NULL 
        || hdr == NULL 
        || __netlink_data_sk == NULL 
        || __netlink_excep_sk == NULL) {
        IO_ERROR("(%d) init task param is null.\n", __LINE__);
        err_code = -EINVAL;
        goto init_direct_failed;
    }

    /*
     * monitor volume count is out of limit.
     */
    if ( init_request->item_cnt > (CDP_MAX_VOLUMES_PER_TASK) ) {
        IO_ERROR("(%d) monitor volume number is out of limit.\n", __LINE__);
        err_code = -EINVAL;
        goto init_direct_failed;
    }

    if ( CDP_CACHED_MODE == (cdp_work_mode)init_request->work_mode ) {
        /* clear zombie task */
        cdp_filter_clear_zombie_task();
        /* get the task id, one client only can run one task, so the task id must be 0 */
        if ( (task_id = cdp_filter_get_task_slot()) != 0 ) {
            IO_ERROR("(%d) error task id is %d.\n", __LINE__, task_id);
            err_code = -EINVAL;
            goto init_direct_failed;
        }
        IO_MSG("(%d) task_id %d\n", __LINE__, task_id);
    }
    else if ( CDP_NO_CACHED_MODE == (cdp_work_mode)init_request->work_mode 
             || CDP_BITMAP_MODE == (cdp_work_mode)init_request->work_mode ) {
        if ( 0 == __task_cnt ) {
            /* there is any active task */
            task_id = cdp_filter_get_task_slot();
        }
        else {
            /* there is a active task */
            task_id = cdp_filter_seek_task_id(init_request);

            if ( -1 == task_id ) {
                /* data source do not matched */
                /* to do... */
                /* support data source modify latter */
                IO_ERROR("(%d) data source do not matched with exist task id %d.\n", __LINE__, task_id);
                err_code = -EINVAL;
            }
            else {
                /* data source matched */
                __task_list[task_id]->pid = hdr->nlmsg_pid;
                /* check if last time scan bitmap success */
                if (false == __task_list[task_id]->success_last_time
                    && NULL != __task_list[task_id]->monitor_info
                    && CDP_BITMAP_MODE == __task_list[task_id]->work_mode)
                {
                    /* merge two buffer of bitmap, this is not the best idea, somewhile it will copy large repeated data */
                    /* it can be optimized later */
                    for (i = 0; i < __task_list[task_id]->monitor_infos; ++i) {
                        if (NULL != __task_list[task_id]->monitor_info[i].pupdate_bitmap) {
                            spin_lock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
                            merge_update_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
                            reinit_seek_info_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
                            spin_unlock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
                        }
                    }

                    __task_list[task_id]->success_last_time = true;
                }

                err_code = CDP_UPDATE_BITMAP_VALID;
                IO_MSG("(%d) CDP_UPDATE_BITMAP_VALID.\n", __LINE__);
            }

            goto init_direct_failed;
        }
    }
    else {
        IO_ERROR("(%d) invalid cdp work mode parameter.\n", __LINE__);
        err_code = -EINVAL;
        goto init_direct_failed;
    }
        IO_MSG("(%d) task_id %d\n", __LINE__, task_id);

    /*
     * update device list
     */
    if ( (err_code = cdp_filter_update_device_list(init_request->item_cnt, init_request->init_info)) ) {
        IO_ERROR("(%d) update device list info failed.\n", __LINE__);
        goto init_direct_failed;
    }

    /*
     * init current cdp_filter_task_info struct.
     */
    if ( 1 ) {
        IO_MSG("(%d) init task and get task id is: %d.\n", __LINE__, task_id);

        __task_list[task_id] = kmalloc(sizeof(struct cdp_filter_task_info), GFP_KERNEL);
        if ( IS_ERR(__task_list[task_id]) ) {
            IO_ERROR("(%d) failed to allocate memory for task_info, the ERROR CODE is: %ld.\n", 
                     __LINE__, err_code = PTR_ERR(__task_list[task_id]));
            goto init_direct_failed;
        }
        memset(__task_list[task_id], 0, sizeof(struct cdp_filter_task_info));

        /*
         * fill up task_info.
         */
        __task_list[task_id]->task_id = task_id;
        __task_list[task_id]->pid = hdr->nlmsg_pid;
        __task_list[task_id]->log_id = 0;
        __task_list[task_id]->sid = 1;
        __task_list[task_id]->mem_used = 0;
        __task_list[task_id]->item_cnt = 0;
        __task_list[task_id]->item_cnt_old = 0;
        __task_list[task_id]->offset = (LOG_FILE_HEAD_SIZE);
        __task_list[task_id]->thread_state = false;
        __task_list[task_id]->need_update = false;
        __task_list[task_id]->flag =false;
        __task_list[task_id]->success_last_time = true;
        __task_list[task_id]->swap_buffer = NULL;
        __task_list[task_id]->swap_buffer_offset = 0;
        __task_list[task_id]->io_thread = NULL;
        __task_list[task_id]->notify_thread = NULL;
        __task_list[task_id]->log_file_handle = NULL;
        __task_list[task_id]->log_file_head = NULL;
        __task_list[task_id]->src_limit.disk_limit = init_request->src_limit.disk_limit*SRC_GRAULAR;
        __task_list[task_id]->src_limit.mem_limit = init_request->src_limit.mem_limit == 0 ? 4 * SRC_GRAULAR : init_request->src_limit.mem_limit*SRC_GRAULAR;
        __task_list[task_id]->src_limit.sync_limit = init_request->src_limit.sync_limit;
        __task_list[task_id]->monitor_infos = 0;
        __task_list[task_id]->monitor_infos_inc = 0;
        __task_list[task_id]->monitor_info = NULL;
        __task_list[task_id]->status = CDP_STATE_NOINIT;
        __task_list[task_id]->work_mode = (cdp_work_mode)init_request->work_mode;
        __task_list[task_id]->job_id = init_request->job_id;
        __task_list[task_id]->io_metadata_entries = NULL;

        __task_list[task_id]->monitor_info = kmalloc((CDP_MAX_VOLUMES_PER_TASK)*sizeof(struct cdp_filter_monitor_info), GFP_KERNEL);
        if ( IS_ERR(__task_list[task_id]->monitor_info) ) {
            IO_ERROR("(%d) failed to allocate memory for monitor_info, the ERROR CODE is: %ld.\n", 
                     __LINE__, err_code = PTR_ERR(__task_list[task_id]->monitor_info));
            goto init_failed;
        }
        memset(__task_list[task_id]->monitor_info, 0, (CDP_MAX_VOLUMES_PER_TASK)*sizeof(struct cdp_filter_monitor_info));

        if (CDP_NO_CACHED_MODE == __task_list[task_id]->work_mode) {
            __task_list[task_id]->swap_buffer = (__u8*)vmalloc(SWAP_BUFFER_SIZE);
            if (NULL == __task_list[task_id]->swap_buffer) {
                IO_ERROR("(%d) failed to allocate memory for swap_buffer, the ERROR CODE is: %ld.\n", 
                         __LINE__, err_code = PTR_ERR(__task_list[task_id]->swap_buffer));
                goto init_failed;
            }
            memset(__task_list[task_id]->swap_buffer, 0, SWAP_BUFFER_SIZE);
        }

        if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
            __task_list[task_id]->io_metadata_entries = (struct cdp_filter_io_metadata *)kmalloc(sizeof(struct cdp_filter_io_metadata)*MAX_IO_METADATA_ENTRIES, GFP_KERNEL);
            if (NULL == __task_list[task_id]->io_metadata_entries) {
                IO_ERROR("(%d) failed to allocate memory for io_metadata_entries, the ERROR CODE is: %ld.\n", 
                         __LINE__, err_code = PTR_ERR(__task_list[task_id]->io_metadata_entries));
                goto init_failed;
            }
            memset(__task_list[task_id]->io_metadata_entries, 0, sizeof(struct cdp_filter_io_metadata)*MAX_IO_METADATA_ENTRIES);
        }

        for ( i = 0; i < init_request->item_cnt; ++i, ++__task_list[task_id]->monitor_infos ) {
            IO_MSG("(%d) dev info, major: %d, minor: %d, sec_size: %d, bl_size: %d, bl_count: %d, dev_name: %s, mount_point: %s, log_name: %s, log_path: %s.\n", 
                   __LINE__, init_request->init_info[i].major_no, init_request->init_info[i].minor_no, 
                   init_request->init_info[i].sec_size, init_request->init_info[i].bl_size, init_request->init_info[i].bl_count, 
                   init_request->init_info[i].dev_name, init_request->init_info[i].mount_point, 
                   init_request->init_info[i].log_dev_name, init_request->init_info[i].log_path);

            __task_list[task_id]->monitor_info[i].dev_no = MKDEV(init_request->init_info[i].major_no, init_request->init_info[i].minor_no);
            __task_list[task_id]->monitor_info[i].sec_size = init_request->init_info[i].sec_size;
            __task_list[task_id]->monitor_info[i].bl_size = init_request->init_info[i].bl_size;
            __task_list[task_id]->monitor_info[i].bl_count = init_request->init_info[i].bl_count;
            strcpy(__task_list[task_id]->monitor_info[i].dev_name, init_request->init_info[i].dev_name);
            strcpy(__task_list[task_id]->monitor_info[i].mount_point, init_request->init_info[i].mount_point);
            strcpy(__task_list[task_id]->monitor_info[i].log_dev_name, init_request->init_info[i].log_dev_name);
            strcpy(__task_list[task_id]->monitor_info[i].log_path, init_request->init_info[i].log_path);

            /*
             * init jbd range.
             */
            __task_list[task_id]->monitor_info[i].jbd_range.start_offset = init_request->init_info[i].jbd_range.start_offset;
            __task_list[task_id]->monitor_info[i].jbd_range.end_offset = init_request->init_info[i].jbd_range.end_offset;


            /*
             * get block device by device number.
             */
            IO_DEBUG("(%d) device num: %d.\n", __LINE__, __task_list[task_id]->monitor_info[i].dev_no);
            __task_list[task_id]->monitor_info[i].device = bdget(__task_list[task_id]->monitor_info[i].dev_no);
            if ( IS_ERR( __task_list[task_id]->monitor_info[i].device) ) {
                IO_ERROR("(%d) failed to get device object for <%d, %d>, the ERROR CODE is: %ld.\n", 
                         __LINE__, MAJOR(__task_list[task_id]->monitor_info[i].dev_no), MINOR(__task_list[task_id]->monitor_info[i].dev_no), 
                         err_code = PTR_ERR(__task_list[task_id]->monitor_info[i].device));
                goto init_failed;
            }

            IO_DEBUG("(%d) devname: %s devno:%u major: %d minor:%d.\n", 
                     __LINE__, __task_list[task_id]->monitor_info[i].dev_name, __task_list[task_id]->monitor_info[i].device->bd_dev,
                     MAJOR(__task_list[task_id]->monitor_info[i].device->bd_dev), MINOR(__task_list[task_id]->monitor_info[i].device->bd_dev));

            if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
                /*
                 * init update bitmap.
                 */
                init_update_bitmap(__task_list[task_id], i);
            }
        }

        if (CDP_CACHED_MODE == __task_list[task_id]->work_mode) 
		{
            /*
            strcpy(__task_list[task_id]->log_path, __task_list[task_id]->monitor_info[0].log_path);
            do_gettimeofday (&tv);
            if ( (err_code = cdp_filter_create_log_file(__task_list[task_id], ((__u64)tv.tv_sec * USEC_PER_SEC + tv.tv_usec))) ) 
			{
                IO_ERROR("(%d) init task, failed to create log file, the ERROR CODE is: %ld.\n", __LINE__, err_code);
                goto init_failed;
            }
            */
        }
		
		IO_MSG("(%d) init_cdp_task id %d\n", __LINE__, task_id);
        __task_list[task_id]->log_file_head = kmalloc((LOG_FILE_HEAD_SIZE), GFP_KERNEL);
        if ( IS_ERR(__task_list[task_id]->log_file_head) ) {
            IO_ERROR("(%d) init task, failed to allocate memory for file head, the ERROR CODE is: %ld.\n", 
                     __LINE__, err_code = PTR_ERR(__task_list[task_id]->log_file_head));
            goto init_failed;
        }
        memset(__task_list[task_id]->log_file_head, 0, (LOG_FILE_HEAD_SIZE));

        err_code = connect_send_recv(init_request);
        if (err_code) {
            IO_ERROR("(%d) connect_sned_recv failed!\n", __LINE__);
            goto init_failed;
        }

        /*
         * init semaphore, 
         * io_list_sem is used for I/O queue, init status is no signal, 
         * thread_mutex is used for update head of I/O log file, init status is signaled.
         */
#if defined(k_4_x_x) || defined(k_3_10_0)
        sema_init(&__task_list[task_id]->io_list_sem, 1);
        sema_init(&__task_list[task_id]->thread_mutex, 1);
#else
        init_MUTEX_LOCKED(&__task_list[task_id]->io_list_sem);
        init_MUTEX(&__task_list[task_id]->thread_mutex);
#endif

        spin_lock_init(&__task_list[task_id]->io_list_lock);
        spin_lock_init(&__task_list[task_id]->sid_list_lock);
        spin_lock_init(&__task_list[task_id]->swap_buffer_lock);
        INIT_LIST_HEAD(&__task_list[task_id]->io_list);
        INIT_LIST_HEAD(&__task_list[task_id]->sid_list);
		IO_MSG("(%d) init_waitqueue_head befor\n", __LINE__);
        init_waitqueue_head(&__task_list[task_id]->sid_wait_queue);
		IO_MSG("(%d) init_waitqueue_head end\n", __LINE__);	
        __task_list[task_id]->status = CDP_STATE_INITIALIZED;
	    IO_MSG("(%d) success task_id %d err_code %ld\n", __LINE__, task_id, err_code);
        kernel_netlink_response(__netlink_sk, CDP_CMD_INIT, task_id, hdr->nlmsg_pid, err_code);
        return;

init_failed:
        if ( NULL != __task_list[task_id]->log_file_handle ) {
            filp_close(__task_list[task_id]->log_file_handle, NULL);
            __task_list[task_id]->log_file_handle = NULL;
        }

        if ( NULL != __task_list[task_id]->monitor_info ) {
            kfree(__task_list[task_id]->monitor_info);
            __task_list[task_id]->monitor_info = NULL;
        }

        if (CDP_NO_CACHED_MODE == __task_list[task_id]->work_mode) {
            if (NULL == __task_list[task_id]->swap_buffer) {
                vfree(__task_list[task_id]->swap_buffer);
                __task_list[task_id]->swap_buffer = NULL;
            }
        }

        if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
            if (NULL == __task_list[task_id]->io_metadata_entries) {
                kfree(__task_list[task_id]->io_metadata_entries);
                __task_list[task_id]->io_metadata_entries = NULL;
            }
        }

        if ( NULL != __task_list[task_id] ) {
            kfree(__task_list[task_id]);
            __task_list[task_id] = NULL;
        }

init_direct_failed:
	IO_ERROR("(%d) failed task_id %d err_code %ld\n", __LINE__, task_id, err_code);
        kernel_netlink_response(__netlink_sk, CDP_CMD_INIT, task_id, hdr->nlmsg_pid, err_code);
    }
}

/**
 * re init task parameters,
 * notes: upper layer update monitor volumes will call this function.
 */
static void 
reinit_cdp_task(struct cdp_filter_init_request* init_request, struct nlmsghdr *hdr)
{
    long int err_code = 0;
    int i = 0, j;
    int task_id = 0;

    if (__netlink_sk == NULL) {
        IO_ERROR("(%d) reinit task param __netlink_sk is null.\n", __LINE__);
        return;
    }

    if (init_request == NULL 
        || hdr == NULL 
        || __netlink_data_sk == NULL 
        || __netlink_excep_sk == NULL) {
        IO_ERROR("(%d) reinit task param is null.\n", __LINE__);
        err_code = EINVAL;
        goto reinit_end;
    }

    task_id = hdr->nlmsg_seq;

    // warning: kernel 3.10.0 return 0 or -1, kernel 4.19.0 return 0 or 1.
    j = cdp_filter_test_task_slot(task_id);
    if ( j == 0 )  {
        IO_ERROR("(%d) reinit task,the task %d is not exsit.\n", __LINE__, task_id);
        err_code = -EINVAL;
        goto reinit_end;
    }

    if ( __task_list[task_id]->monitor_infos + init_request->item_cnt > (CDP_MAX_VOLUMES_PER_TASK) ) {
        IO_ERROR("(%d) reinit task, monitor volume num is out of limit, original vloume num:%d, inc volume num:%d.\n", 
                 __LINE__, __task_list[task_id]->monitor_infos, init_request->item_cnt);
        err_code = -EINVAL;
        goto reinit_end;
    }

    if ( (err_code = cdp_filter_update_device_list(init_request->item_cnt, init_request->init_info)) ) {
        IO_ERROR("(%d) update device list info failed\n", __LINE__);
        goto reinit_end;
    }
    __task_list[task_id]->monitor_infos_inc = init_request->item_cnt;
    for ( i = __task_list[task_id]->monitor_infos, j = 0;
          j < init_request->item_cnt; 
          ++i, ++j, ++__task_list[task_id]->monitor_infos ) {

        IO_MSG("(%d) original dev info: major: %d, minor: %d, dev_name: %s, mount_point: %s, log_name: %s, log_path: %s.\n", 
               __LINE__, init_request->init_info[j].major_no, init_request->init_info[j].minor_no, init_request->init_info[j].dev_name, 
                init_request->init_info[j].mount_point, init_request->init_info[j].log_dev_name, init_request->init_info[j].log_path);

        __task_list[task_id]->monitor_info[i].dev_no = MKDEV(init_request->init_info[j].major_no, init_request->init_info[j].minor_no) ;
        __task_list[task_id]->monitor_info[i].sec_size = init_request->init_info[j].sec_size;
        __task_list[task_id]->monitor_info[i].bl_size = init_request->init_info[j].bl_size;
        strcpy(__task_list[task_id]->monitor_info[i].dev_name, init_request->init_info[j].dev_name);
        strcpy(__task_list[task_id]->monitor_info[i].mount_point, init_request->init_info[j].mount_point);
        strcpy(__task_list[task_id]->monitor_info[i].log_dev_name, init_request->init_info[j].log_dev_name);
        strcpy(__task_list[task_id]->monitor_info[i].log_path, init_request->init_info[j].log_path);

        /*
         * get block device by device number.
         */
        IO_DEBUG("(%d) device num: %d.\n", __LINE__, __task_list[task_id]->monitor_info[i].dev_no);
        __task_list[task_id]->monitor_info[i].device  = bdget(__task_list[task_id]->monitor_info[i].dev_no);
        if ( IS_ERR( __task_list[task_id]->monitor_info[i].device) ) {
            IO_ERROR("(%d) failed to get device object for <%d, %d>, the ERROR CODE is: %ld.\n", 
                     __LINE__, MAJOR(__task_list[task_id]->monitor_info[i].dev_no), MINOR(__task_list[task_id]->monitor_info[i].dev_no), 
                     err_code = PTR_ERR(__task_list[task_id]->monitor_info[i].device));
            goto reinit_end;
        }
    }

reinit_end:
    kernel_netlink_response(__netlink_sk, CDP_CMD_REINIT, task_id, hdr->nlmsg_pid, err_code);
}

/**
 * deal with block I/O.
 */
bool cdp_filter_deal_bio(uint task_id, uint index, struct bio *bio, unsigned long long *dataoffset, uint *datalength, __u64 timestamp, unsigned long long original_offset)
{
#if defined(k_4_x_x)
    struct bio_vec bvec;
    struct bvec_iter iter;
    sector_t start_sector;
    uint seg_count;
#else
    uint seg_cnt, i;
#endif
    uint has_copied_length;
    void* vir_addr;
    struct cdp_filter_data_info *data_info = NULL;
    unsigned long long dataoffset2 = 0;
    uint datalength2 = 0;
    uint offset = 0;
    int order = 0;


    if (bio == NULL || dataoffset == NULL || datalength == NULL) {
        IO_ERROR("(%d) deal bio unsuccessful because parameter is null.\n", __LINE__);
        return false;
    }

    dataoffset2 = *dataoffset;
    datalength2 = *datalength;
    data_info = kmalloc(sizeof(struct cdp_filter_data_info), GFP_KERNEL);
    if (NULL == data_info || IS_ERR(data_info) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(data_info));
        return false;
    }

    order = get_order(*datalength);
    IO_DEBUG("(%d) length: %u, order: %d, PAGC_SIZE:%lu\n", __LINE__, *datalength, order, PAGE_SIZE);
#if defined(k_4_x_x)
    data_info->data = ((*datalength) <= PAGE_SIZE) ? kmalloc(*datalength, GFP_KERNEL) : (__u8 *)__get_free_pages(GFP_KERNEL, order);
#else
    data_info->data = ((*datalength) <= PAGE_SIZE) ? kmalloc(*datalength, GFP_KERNEL) : (__u8 *)__get_free_pages(GFP_KERNEL, order);
#endif

    spin_lock(&__task_list[task_id]->io_list_lock);
    IO_DEBUG("(%d)  memused: %lld. kmalloc %u.\n", __LINE__, __task_list[task_id]->mem_used, *datalength);
    spin_unlock(&__task_list[task_id]->io_list_lock);

    // Check if memory allocation failed
    if (NULL == data_info->data) {
        spin_lock(&__task_list[task_id]->io_list_lock);
        IO_ERROR("(%d) memused: %lld. kmalloc %u null.\n", __LINE__, __task_list[task_id]->mem_used, *datalength);
        spin_unlock(&__task_list[task_id]->io_list_lock);

        if(NULL == data_info->data) {
            kfree(data_info);
            data_info = NULL;
        }

        if(*datalength <= PAGE_SIZE) {
            IO_ERROR("(%d) can not malloc any memory.\n", __LINE__);
            __task_list[task_id]->flag = false;
            kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_id, __task_list[task_id]->pid, ENOMEM);
            return false;
        } else {
            IO_MSG("(%d) kmalloc again.\n", __LINE__);
            *datalength = (*datalength)/2;
            IO_DEBUG("(%d) first dataoffset: %llu, datalength: %u.\n", __LINE__, *dataoffset, *datalength);
            if(!cdp_filter_deal_bio(task_id, index, bio, dataoffset, datalength, timestamp, original_offset)) {
                IO_ERROR("(%d) failed to deal io pack.\n", __LINE__);
                return false;
            }
            IO_DEBUG("(%d) second dataoffset: %llu, datalength: %u.\n", __LINE__, *dataoffset, *datalength);
            *dataoffset = dataoffset2 + (*datalength);
            *datalength = datalength2 - (*datalength);
            IO_DEBUG("(%d) third dataoffset: %llu, datalength: %u.\n", __LINE__, *dataoffset, *datalength);
            return cdp_filter_deal_bio(task_id, index, bio, dataoffset, datalength, timestamp, original_offset);
        }
    }

    if ( IS_ERR(data_info->data) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(data_info->data));
        if (data_info != NULL) {
            kfree(data_info);
            data_info = NULL;
        }

        return false;
    }

    /*
     * fill data_info.
     */
    data_info->item_head.data_offset = *dataoffset;
    data_info->volume_index = index;
    data_info->job_id = __task_list[task_id]->job_id;
    data_info->item_head.io_timestamp = timestamp;
    data_info->item_head.item_type = DATA_TYPE_IO;

    has_copied_length = (uint)(*dataoffset - original_offset);

#if defined(k_4_x_x)
    // 1. 获取 bio 的基本信息
    start_sector = bio->bi_iter.bi_sector;
    seg_count = 0;
    // 2. 遍历所有 segment
    bio_for_each_segment(bvec, bio, iter) {
        // 检查当前segment是否在我们需要复制的数据范围内
        if (offset + bvec.bv_len > has_copied_length && 
            offset < has_copied_length + *datalength) {

            // 计算实际需要复制的长度
            unsigned int copy_len = bvec.bv_len;
            unsigned int src_offset = 0;
            unsigned int dst_offset = 0;

            // 如果segment起始位置在has_copied_length之前，需要调整
            if (offset < has_copied_length) {
                src_offset = has_copied_length - offset;
                copy_len -= src_offset;
            }

            // 如果segment结束位置超出了所需数据范围，需要调整
            if (offset + bvec.bv_len > has_copied_length + *datalength) {
                copy_len = has_copied_length + *datalength - offset - src_offset;
            }

            // 计算目标缓冲区偏移
            dst_offset = offset > has_copied_length ? (offset - has_copied_length) : 0;

             // 确保copy_len和dst_offset有效后再进行内存操作
            if (copy_len > 0 && dst_offset < *datalength) {
                vir_addr = kmap(bvec.bv_page);
                memcpy(data_info->data + dst_offset, vir_addr + bvec.bv_offset + src_offset, copy_len);
                kunmap(bvec.bv_page);
            }
        }
        offset += bvec.bv_len;
        seg_count++;
    }

    IO_DEBUG("(%d) start_sector:%lu seg_count:%u, dataoffset:%llu, datalength:%u, has_copied_length:%u, biostartoffset:%llu, biolength:%u.\n",
             __LINE__, start_sector, seg_count, *dataoffset, offset - has_copied_length, has_copied_length, original_offset, offset);
#else
    seg_cnt = bio->bi_vcnt;
    for ( i = 0; 
          i < seg_cnt && bio->bi_io_vec[i].bv_len != 0 && (offset + bio->bi_io_vec[i].bv_len) <= (has_copied_length + *datalength); 
          ++i ) {
        if(offset >= has_copied_length) {
            flush_dcache_page(bio->bi_io_vec[i].bv_page);
            vir_addr = kmap(bio->bi_io_vec[i].bv_page) ;
            memcpy(data_info->data + offset - has_copied_length, vir_addr + bio->bi_io_vec[i].bv_offset, bio->bi_io_vec[i].bv_len);
            kunmap(bio->bi_io_vec[i].bv_page);
        }

        offset += bio->bi_io_vec[i].bv_len;
    }

    IO_DEBUG("(%d) seg_cnt:%u i:%u, dataoffset:%llu, datalength:%u, has_copied_length:%u, biostartoffset:%llu, biolength:%u.\n",
             __LINE__, seg_cnt, i, *dataoffset, offset - has_copied_length, has_copied_length, original_offset, bio->bi_size);
#endif
    data_info->item_head.data_length = offset - has_copied_length;
    strncpy(&data_info->item_head.mount_point[0], &__task_list[task_id]->monitor_info[index].mount_point[0], DISK_DOS_NAME_LEN);

    /*
     * inset data_info to io list and increase used memory.
     */
    /*
     * make sure cdp_filter has enough mem
     */
/*    long long my_mem_used;
    while( true ) {
        my_mem_used  = 0;
        spin_lock(&__task_list[task_id]->io_list_lock);
        my_mem_used = __task_list[task_id]->mem_used + (long long)(data_info->item_head.data_length);
        IO_MSG("(%d) my_mem_used: %lld, limit: %lld. data_length:%lld\n",
            __LINE__, my_mem_used, __task_list[task_id]->src_limit.mem_limit,(long long)(data_info->item_head.data_length) );
        spin_unlock(&__task_list[task_id]->io_list_lock);
        //if (  my_mem_used  >= __task_list[task_id]->src_limit.mem_limit ) {
        if (  my_mem_used  >= 1073741824 ) {
            IO_MSG(" in test mem_used\n");
            msleep(100);
        } else {
            break;
        }
    }
  */  /**/
    spin_lock(&__task_list[task_id]->io_list_lock);
    list_add_tail(&data_info->list, &__task_list[task_id]->io_list);
    __task_list[task_id]->mem_used += (long long)(data_info->item_head.data_length);

    if ( __task_list[task_id]->mem_used >= __task_list[task_id]->src_limit.mem_limit ) {
        if (CDP_NO_CACHED_MODE == __task_list[task_id]->work_mode) {
            IO_MSG("(%d) mem_used: %lld, limit: %lld.\n", 
                   __LINE__, __task_list[task_id]->mem_used, __task_list[task_id]->src_limit.mem_limit);

            __task_list[task_id]->status = CDP_STATE_PAUSE;
        }
        else {
            IO_ERROR("(%d) mem_used: %lld, limit: %lld.\n", 
                     __LINE__, __task_list[task_id]->mem_used, __task_list[task_id]->src_limit.mem_limit);
            IO_MSG("(%d) mem_used: %lld, limit: %lld.\n", 
                     __LINE__, __task_list[task_id]->mem_used, __task_list[task_id]->src_limit.mem_limit);
            IO_MSG("(%d) CDPCDPCDPCDP\n", __LINE__);
    
            __task_list[task_id]->flag = false;
            spin_unlock(&__task_list[task_id]->io_list_lock);
            up(&__task_list[task_id]->io_list_sem);
            kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_id, __task_list[task_id]->pid, CDP_RESOURCE_MEM_LACK_ERROR);
            return false;
        }
    }

    spin_unlock(&__task_list[task_id]->io_list_lock);
    *datalength = data_info->item_head.data_length;
    IO_DEBUG("(%d) firth dataoffset: %llu, datalength: %u.\n", __LINE__, *dataoffset, *datalength);
    return true;
}

bool 
cdp_filter_hook_io_pack(uint task_id, uint index, struct bio *bio)
{
    unsigned long long data_offset;
    unsigned long long data_part_offset;
    uint data_length;
    struct timeval tv;
    __u64 timestamp = 0;
#if defined(k_4_x_x)
    struct bio_vec bv;
    struct bvec_iter iter;
    int vec_count = 0;
#endif

    if ( ((MAX_TASK_NUM) <= task_id) 
        || NULL == __task_list[task_id]
        || __task_list[task_id]->monitor_infos <= index
        || NULL == bio) {
        IO_ERROR("(%d) invalid function parameter taskid: %u, index: %u.\n", __LINE__, task_id, index);
        return false;
    }

    /*
     * data less bio.
     */
    if ( !bio_has_data(bio)  ) {
        IO_ERROR("(%d) bio has no data.\n", __LINE__);
        return true;
    }

    /* I/O offset, relative to device */
#if defined(k_4_x_x)
    data_offset = ((unsigned long long)bio->bi_iter.bi_sector) * ((unsigned long long)__task_list[task_id]->monitor_info[index].sec_size);
#else
    data_offset = ((unsigned long long)bio->bi_sector) * ((unsigned long long)__task_list[task_id]->monitor_info[index].sec_size);
#endif
    /* I/O offset, relative to partition */
    if (NULL != __task_list[task_id]->monitor_info[index].device->bd_part) {
        /* file system build on partition device */
        data_part_offset = data_offset - ((unsigned long long)__task_list[task_id]->monitor_info[index].device->bd_part->start_sect * (unsigned long long)__task_list[task_id]->monitor_info[index].sec_size);
    }
    else {
        /* file system build on no partition device or LVM logical volume */
        data_part_offset = data_offset;
    }
    /* residual I/O count, always equal to block size of the file system on partition or device */
#if defined(k_4_x_x)
    data_length = 0;
    /* Calculate total data length by iterating all bio_vec entries */
    bio_for_each_segment(bv, bio, iter) {
        data_length += bv.bv_len;
        vec_count++;
    }
    IO_DEBUG("(%d) bio data stats: calculated_len=%u, vec_count=%d\n", __LINE__, data_length, vec_count);
#else
    data_length = bio->bi_size;
#endif

    IO_DEBUG("(%d) block data_offset: %lld, data_part_offset: %lld, data_length: %u.\n", __LINE__, data_offset, data_part_offset, data_length);

    /*
     * ignore i/o range in jbd range.
     */
    if ( data_part_offset >= __task_list[task_id]->monitor_info[index].jbd_range.start_offset 
        && (data_part_offset + data_length) <= __task_list[task_id]->monitor_info[index].jbd_range.end_offset ) {
        IO_DEBUG("(%d) ignore i/o range in jbd range.\n", __LINE__);
        return true;
    }

    /*
     * add io timestamp before slicing block io, 
     * a block io must recover together by mark the same timestamp.
     */
    do_gettimeofday (&tv);
    timestamp = ((__u64)tv.tv_sec * USEC_PER_SEC + tv.tv_usec);

    if(!cdp_filter_deal_bio(task_id, index, bio, &data_part_offset, &data_length, timestamp, data_part_offset)) {
        IO_ERROR("(%d) failed to deal io pack.\n", __LINE__);
        return false;
    }

    up(&__task_list[task_id]->io_list_sem);
    return true;
}

#if defined(k_4_x_x)
blk_qc_t
#elif defined(k_3_10_0)
void 
#else
int 
#endif
cdp_filter_make_request(struct request_queue *q, struct bio *bio)
{
    uint i = 0, j = 0, item_cnt = 0;
    dev_t dev_no = 0;
    int slot = 0;

    if (q == NULL || bio == NULL) {
        IO_ERROR("(%d) parameter is NULL error.\n", __LINE__);
#if defined(k_3_10_0)
        return;
#else
        return 0;
#endif
    }

#if defined(k_4_x_x)
    dev_no = MKDEV(bio->bi_disk->major, bio->bi_disk->first_minor);
#else
    dev_no = bio->bi_bdev->bd_dev;
#endif
    slot = cdp_filter_calc_dev_slot(MAJOR(dev_no), MINOR(dev_no)); 
    if (slot < 0) {
        IO_ERROR("(%d) can not find the device, error slot: %d.\n", __LINE__, slot);
        goto pass_bio;
    }

    /*
     * return data direction, READ or WRITE.
     */
    if ( READ == bio_data_dir(bio) ) {
        goto pass_bio;
    }

    for ( j = 0; j < (MAX_TASK_NUM) && __task_list[j] && __task_list[j]->flag; ++j ) {
        item_cnt = __task_list[j]->monitor_infos;
        for ( i = 0; i < item_cnt; ++i ) {
            /*
             * only hook bio under conditions as follows: 
             * device in monitor device list and task is still on running state.
             */
#if defined(k_4_x_x)
            if ( __task_list[j]->monitor_info[i].device->bd_disk == bio->bi_disk 
                && __task_list[j]->flag 
                && __task_list[j]->thread_state ) {
#else
            if ( __task_list[j]->monitor_info[i].device->bd_disk == bio->bi_bdev->bd_disk 
                && __task_list[j]->flag 
                && __task_list[j]->thread_state ) {
#endif
                    if(__task_list[j]->monitor_info[i].device->bd_part == NULL) {
                        IO_DEBUG("(%d) block_device_bdpart is null.\n", __LINE__);
                        /* file system build on no partition device or LVM logical volume */
                        goto hook_bio;
                    }

#if defined(k_4_x_x)
                    if(bio->bi_iter.bi_sector >= __task_list[j]->monitor_info[i].device->bd_part->start_sect
                       && (__task_list[j]->monitor_info[i].device->bd_part->start_sect 
                           + __task_list[j]->monitor_info[i].device->bd_part->nr_sects) >= (bio->bi_iter.bi_sector + bio_sectors(bio))) {
#else
                    if(bio->bi_sector >= __task_list[j]->monitor_info[i].device->bd_part->start_sect
                       && (__task_list[j]->monitor_info[i].device->bd_part->start_sect 
                           + __task_list[j]->monitor_info[i].device->bd_part->nr_sects) >= (bio->bi_sector + bio_sectors(bio))) {
#endif 
                            IO_DEBUG("(%d) block_device_bdpart startSec:%lu, nr_sects:%lu.\n", 
                                     __LINE__, (unsigned long)__task_list[j]->monitor_info[i].device->bd_part->start_sect, 
                                     (unsigned long)__task_list[j]->monitor_info[i].device->bd_part->nr_sects);
                            /* file system build on partition device and i/o happen in monitor partition range */
                            goto hook_bio;
                    }
            }
        }
    }

   goto pass_bio;

hook_bio:
    IO_DEBUG("(%d) bio data dir %d\n", __LINE__, bio_data_dir(bio));
#if defined(k_4_x_x)
    IO_DEBUG("(%d) bio op %d\n", __LINE__, bio_op(bio));
#else
    IO_DEBUG("(%d) bio rw %d\n", __LINE__, bio_rw(bio));
#endif
    if ( cdp_filter_task_is_alive(__task_list[j]) ) {
        if(!cdp_filter_hook_io_pack(__task_list[j]->task_id, i, bio)) {
            IO_ERROR("(%d) failed to hook io pack.\n", __LINE__);
        }
    }

pass_bio:
    if (slot >= 0 && __dev_list[slot]) {
#if defined(k_3_10_0)
        __dev_list[slot]->make_request_fn(q, bio);
#elif defined(k_4_x_x)
        blk_qc_t ret;
        ret = __dev_list[slot]->make_request_fn(q, bio);
#else
        int ret;
        ret = __dev_list[slot]->make_request_fn(q, bio);
#endif
        // clear error task mem
        if (j < (MAX_TASK_NUM) && __task_list[j] && __task_list[j]->flag == false) {
            IO_DEBUG("(%d) clear error task.\n", __LINE__);
            cdp_filter_clear_zombie_task();
        }
#if defined(k_3_10_0)
        return;
#else
        return ret;
#endif
    } else {
        IO_ERROR("(%d) todo error.\n", __LINE__);
        kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, __task_list[j]->task_id, __task_list[j]->pid, slot);
    }

#if defined(k_3_10_0)
    return;
#else
    return 0;
#endif
}

struct sk_buff* 
cdp_filter_data_sk_buff_create(struct cdp_filter_task_info *task_info)
{
    struct sk_buff *skb = NULL;
    struct nlmsghdr *hdr;
    struct cdp_filter_data_request *data_request;
    /* ISO C90 warning */
#if defined(k_4_x_x) || (defined(k_3_10_0) || defined(k_2_6_24) || defined(k_2_6_32)) && BITS_PER_LONG > 32
    unsigned int old_tail;
#else
    unsigned char *old_tail;
#endif
    int size;

    if ( NULL == task_info ) {
        IO_ERROR("(%d) parameter is NULL.\n", __LINE__);
        return NULL;
    }

    size = NLMSG_SPACE(sizeof(struct cdp_filter_data_request));
    if ( NULL == (skb = alloc_skb(size, GFP_KERNEL)) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(skb));
        return NULL;
    }

    old_tail = skb->tail;

    /* disable broadcast */
#if defined(k_2_6_9) || defined(k_2_6_13)
    NETLINK_CB(skb).dst_groups = 0;
#else
    NETLINK_CB(skb).dst_group = 0;
#endif

    /* data buffer is insufficient */
    if ( skb_tailroom(skb) < (int)NLMSG_SPACE(size - sizeof(*hdr)) ) {
        IO_ERROR("(%d) data buffer is insufficient.\n", __LINE__);
        return NULL;
    }

    /* fill nlmsghdr and stretch data buffer */
#if defined(k_2_6_9)
    hdr = __nlmsg_put(skb, task_info->pid, 0, CDP_CMD_DATA, size-sizeof(*hdr));
#else
    hdr = __nlmsg_put(skb, task_info->pid, 0, CDP_CMD_DATA, size-sizeof(*hdr), 0);
#endif

    /* set nlmsg_len */
    hdr->nlmsg_len = skb->tail - old_tail;

    /* fill data buffer */
    data_request = (struct cdp_filter_data_request*)(char*)NLMSG_DATA(hdr);
    data_request->task_id = task_info->task_id;
    strncpy(data_request->file_name, task_info->log_file_name, DISK_DOS_NAME_LEN);

    return skb;
}

/**
 * fill the head of I/O log file and send I/O data to upper layer.
 */
void
kernel_netlink_data_notify(struct cdp_filter_task_info *task_info)
{
    int ret = 0;
    struct sk_buff *skb = NULL;
    mm_segment_t old_fs;
    ssize_t vfs_ret;
	
//cdp modify
	return;

    if (NULL == task_info || NULL == task_info->log_file_handle) {
        IO_ERROR("(%d) kernel netlink data notify paramter is NULL.\n", __LINE__);
        return;
    }

    if ( down_interruptible(&task_info->thread_mutex) ) {
        IO_ERROR("(%d) down_interruptible call of task <%d> interrupted by system.\n", 
                 __LINE__, task_info->task_id);
        return;
    }

    task_info->log_file_head->first_item_no = 0;
    task_info->log_file_head->items = task_info->item_cnt;

    task_info->log_file_handle->f_pos = 0;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_ret = vfs_write(task_info->log_file_handle, (char __user *)task_info->log_file_head, 
                        (LOG_FILE_HEAD_SIZE), &task_info->log_file_handle->f_pos);
    set_fs(old_fs);

    if (vfs_ret < LOG_FILE_HEAD_SIZE) {
        IO_ERROR("(%d) vfs write file head error, return value: %u.\n", __LINE__, (unsigned int)vfs_ret); 
        kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_info->task_id, task_info->pid, CDP_DISK_NO_SPACE);
        /* 
         * upper layer use blocking mode to get data socket message (not a good idea), 
         * so can not return direct even exception occur, 
         * otherwise will cause upper layer blocked and never to be awakened.
         */
    }

    if ( (LOG_ITEMS_PER_FILE) <= task_info->item_cnt && task_info->log_file_handle != NULL) {
        filp_close(task_info->log_file_handle, NULL);
        task_info->log_file_handle = NULL;
    }

    IO_DEBUG("(%d) send data notification, file name: %s, old items: %d, items: %d.\n", 
             __LINE__, task_info->log_file_name, task_info->item_cnt_old, task_info->item_cnt);

    task_info->item_cnt_old = task_info->item_cnt;

    skb = cdp_filter_data_sk_buff_create(task_info);

    if ( NULL != skb && cdp_filter_task_is_alive(task_info) ) {
        /* set nonblock to 0 mean block until has available receive buffer */
        ret = netlink_unicast(__netlink_data_sk, skb, task_info->pid, 0);
    }

    if ( ret < 0 ) {
        IO_ERROR("(%d) failed to send netlink data notification, return code is: %d.\n", __LINE__, ret);
    }

    up(&task_info->thread_mutex);
}

/**
 * bio handle thread
 */
static int 
cdp_filter_thread_handler(void *task)
{
    struct cdp_filter_data_info *pos = NULL, *item = NULL;
    struct cdp_filter_task_info *task_info = NULL;
    

    char *send_buf = NULL;
    int rets, temp_time;
    struct kvec send_vec;
    struct msghdr send_msg;


    if (task == NULL) {
        IO_ERROR("(%d) cdp_filter_thread_handler parameter is NULL.\n", __LINE__);
        return 0;
    }

    send_buf = kmalloc(4096, GFP_KERNEL);
    if (send_buf == NULL) {
        IO_ERROR("(%d) client: send_buf kmalloc error!\n", __LINE__);
        return -1; 
    }	
    memset(&send_msg, 0, sizeof(send_msg));

    task_info = (struct cdp_filter_task_info*)task;
    IO_MSG("(%d) io_thread\n", __LINE__);
    if ( NULL != task_info ) {
        uint item_len = 0;
        __u64 item_offset = 0;
        long long mem_used = 0;
        uint cur_len = 0;
        uint cur_offset = 0;
        long ret;
	    int try_count=0;
        // mm_segment_t old_fs;
        struct list_head tmp_io_list;
        // ssize_t vfs_ret;

        struct cdp_filter_item_head item_head;

        // send jobid
        memset(send_buf, 0, 4096);
        send_vec.iov_base = send_buf;
        send_vec.iov_len = sizeof(int);
        memcpy(send_buf, (char *)&task_info->job_id, sizeof(int));
        IO_MSG("(%d) start send jobid %u \n", __LINE__, task_info->job_id);
        rets = kernel_sendmsg(sock, &send_msg, &send_vec, 1, sizeof(int));
        if (rets < 0) {
            IO_ERROR("(%d) client: kernel_sendmsg error!\n", __LINE__);
            goto io_thread_exit;
        }

        /* thread is running */
        task_info->thread_state = 1;
        do {
            if (!cdp_filter_task_is_alive(task_info)) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(1*HZ);
                if ( kthread_should_stop() ) {
                    IO_MSG("(%d) stop io thread.\n", __LINE__);
                    break;
                }
                continue;
            }
            task_info->need_update = true;

            if ( kthread_should_stop() ) {
                break;
            }

            /* using two different waiting way to break potential deadlock */
            if ( !task_info->thread_state ) {
				IO_MSG("(%d) thread_state %d\n", __LINE__, task_info->thread_state);
                IO_DEBUG("(%d) Prepare to exit thread <task id: %d>.\n", __LINE__, task_info->task_id);
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(1*HZ);
            } else {
                /*
                 * Attempts to acquire the semaphore. If no more tasks are allowed to
                 * acquire the semaphore, calling this function will put the task to sleep.
                 * If the sleep is interrupted by a signal, this function will return -EINTR.
                 * If the semaphore is successfully acquired, this function returns 0.
                 */
                if ( down_interruptible(&task_info->io_list_sem) ) {
                    IO_MSG("(%d) down\n", __LINE__);
                    IO_ERROR("(%d) task <%d> thread terminated caused by interrupt signals.\n", __LINE__, task_info->task_id);
                    kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_info->task_id, task_info->pid, -EINVAL);
                    return -EINTR;
                }
            }

            if ( kthread_should_stop() ) {
                break;
            }

            INIT_LIST_HEAD(&tmp_io_list);
            spin_lock(&task_info->io_list_lock);
            task_info->mem_used -= mem_used;
            list_splice_init(&task_info->io_list, &tmp_io_list);
            task_info->need_update = false;
            IO_DEBUG("(%d) task_info->mem_used: %lld, mem_used: %lld.\n", __LINE__, task_info->mem_used, mem_used);
            spin_unlock(&task_info->io_list_lock);

            mem_used = 0;

            /*
             * traverse I/O list and handle I/O item.
             */
            list_for_each_entry_safe(pos, item, &tmp_io_list, list) {
                item_len = pos->item_head.data_length;
                item_offset = pos->item_head.data_offset;
                if (pos->item_head.item_type == DATA_TYPE_IO) {
                    mem_used += item_len;
                }
                cur_offset = 0;
                while ( item_len && cdp_filter_task_is_alive(task_info) ) {

                    if (CDP_CACHED_MODE == task_info->work_mode) 
                    {
                        /*
                        if ( task_info->item_cnt >= (LOG_ITEMS_PER_FILE) ) 
                        {
                            IO_DEBUG("(%d) succeed to receive update notification, task id: %d, file name: %s, items: %d.\n", 
                                     __LINE__, task_info->task_id, task_info->log_file_name, (LOG_ITEMS_PER_FILE));
                            kernel_netlink_data_notify(task_info);
                            ret = cdp_filter_create_log_file(task_info, pos->item_head.io_timestamp);
                            IO_MSG("(%d) io_creat ret %ld", __LINE__, ret);
                            if ( ret ) 
                            {
                                IO_ERROR("(%d) Abort <%d>-dedicated thread due to fail to create log file.\n", 
                                         __LINE__, task_info->task_id);
                                kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_info->task_id, task_info->pid, CDP_DISK_NO_SPACE);

                                // to do...
                                // this not a good idea to quit io thread, 
                                // will cause io pack stacking in io list if upper layer do not send CDP_CMD_DEL command to kernel.

                                goto io_thread_exit;
                            }
                        }
                        */

                        IO_DEBUG("(%d) start init head jobid=%u \n", __LINE__, pos->job_id);

                        cur_len = (item_len > (MAX_LOG_ITEM_SIZE)) ? (MAX_LOG_ITEM_SIZE) : item_len;
                        memset(&item_head, 0, (LOG_HEAD_SIZE));
                        item_head.data_length = cur_len;
                        item_head.data_offset = item_offset;
                        item_head.io_timestamp = pos->item_head.io_timestamp;
                        item_head.item_type = pos->item_head.item_type;
                        item_head.item_id = __logitemcount++;
                        item_head.volume_index = pos->volume_index;
                        item_head.job_id = pos->job_id;

                        IO_DEBUG("(%d) item_id:%lld, io_timestamp:%lld, item_type:%u.\n", __LINE__, item_head.item_id, item_head.io_timestamp, item_head.item_type);
                        strncpy(item_head.mount_point, pos->item_head.mount_point, DISK_DOS_NAME_LEN);

                        // task_info->log_file_handle->f_pos = task_info->offset;
                        memset(send_buf, 0, MAX_LOG_ITEM_SIZE);
                        send_vec.iov_base = send_buf;
                        send_vec.iov_len = LOG_HEAD_SIZE;
                        memcpy(send_buf, (char*)&item_head, LOG_HEAD_SIZE);
                        IO_DEBUG("(%d) start send head iov_len %zu \n", __LINE__, send_vec.iov_len);
                        while (0 > kernel_sendmsg(sock, &send_msg, &send_vec, 1, LOG_HEAD_SIZE) && (try_count < 3)) {
                            IO_ERROR("(%d) client: log head kernel_sendmsg error, try again!\n", __LINE__);
                            if (kthread_should_stop()) {
                                break;
                            }
                            msleep(10000);
                            try_count++;
                        }
                        if ( try_count == 3 ){
                            task_info->status = CDP_STATE_SENDE;
                            goto io_thread_exit;
                            // task_info->status = CDP_STATE_SENDE;
                        }
                        IO_DEBUG("(%d) end send head iov_len %zu \n", __LINE__, send_vec.iov_len);
                        if (kthread_should_stop()) {
                            break;
                        }

                        /*
                        old_fs = get_fs();
                        set_fs(KERNEL_DS);
                        vfs_ret = vfs_write(task_info->log_file_handle, (char*)&item_head, (LOG_HEAD_SIZE), &task_info->log_file_handle->f_pos);
                        set_fs(old_fs);

                        if(vfs_ret < LOG_HEAD_SIZE) {
                            IO_ERROR("(%d) vfs write log item head error, return value:%u.\n", __LINE__, (unsigned int)vfs_ret);
                            kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_info->task_id, task_info->pid,  CDP_DISK_NO_SPACE);

                            //to do...
                            //this not a good idea to quit io thread, 
                            //will cause io pack stacking in io list if upper layer do not send CDP_CMD_DEL command to kernel.

                            goto io_thread_exit;
                        }
                        */

                        task_info->offset += (LOG_HEAD_SIZE);

                        if(item_head.item_type == DATA_TYPE_IO) {
                            // task_info->log_file_handle->f_pos = task_info->offset;
                            memset(send_buf, 0, MAX_LOG_ITEM_SIZE);
                            send_vec.iov_base = send_buf;
                            send_vec.iov_len = MAX_LOG_ITEM_SIZE;
                            memcpy(send_buf, pos->data + cur_offset, cur_len);
                            IO_DEBUG("(%d) start send data iov_len %zu \n", __LINE__, send_vec.iov_len);
                            try_count = 0;
                            while (0 > kernel_sendmsg(sock, &send_msg, &send_vec, 1, MAX_LOG_ITEM_SIZE) && (try_count < 3)) {
                                IO_ERROR("(%d) client: log data kernel_sendmsg error, try again!\n", __LINE__);
                                if (kthread_should_stop()) {
                                    break;
                                }
                                msleep(10000);
                                try_count++;
                            }
                            if ( try_count == 3 ) {
                                task_info->status = CDP_STATE_SENDE;
                                goto io_thread_exit;
                                // task_info->status = CDP_STATE_SENDE;
                            }
                            IO_DEBUG("(%d) end send data iov_len %zu \n", __LINE__, send_vec.iov_len);
                            if (kthread_should_stop()) {
                                break;
                            }

                            /*
                            old_fs = get_fs();
                            set_fs(KERNEL_DS);
                            vfs_ret = vfs_write(task_info->log_file_handle, pos->data+cur_offset, cur_len, &task_info->log_file_handle->f_pos);
                            set_fs(old_fs);

                            if(vfs_ret < cur_len) {
                                IO_ERROR("(%d) vfs write log item data error, return value:%u.\n", __LINE__, (unsigned int)vfs_ret);
                                kernel_netlink_response(__netlink_excep_sk, CDP_CMD_EXCP, task_info->task_id, task_info->pid, CDP_DISK_NO_SPACE);

                                // to do...
                                // this not a good idea to quit io thread,
                                // will cause io pack stacking in io list if upper layer do not send CDP_CMD_DEL command to kernel.

                                goto io_thread_exit;
                            }
                            */
                        } else {
                            IO_MSG("(%d) write consistent flag succeed, timestamp: %lld.\n", __LINE__, item_head.io_timestamp);
                        }

                        IO_DEBUG("(%d) I/O item info, task is: %d,type:%u, offset of item: %lld, length of item: %d, offset of data: %d, offset of file: %lld.\n", 
                                __LINE__, task_info->task_id,item_head.item_type, item_head.data_offset, 
                                item_head.data_length, cur_offset, task_info->offset);

                        task_info->offset += (MAX_LOG_ITEM_SIZE);
                        IO_DEBUG("(%d) cur_len  %d \n", __LINE__, cur_len);
                        cur_offset += cur_len;
                        item_len -= cur_len;
                        item_offset += cur_len;
                        ++task_info->item_cnt;
                    }
                    else if (CDP_NO_CACHED_MODE == task_info->work_mode) {
                        /*
                        * CDP_NO_CACHED_MODE mode swap I/O data with upper layer by memory buffer, comprises the following steps: 
                        * 1.judge whether memory buffer out of limit, if so, enable update bitmap and set flag;
                        * 2.waiting for upper layer inform synchronization data;
                        */
                        do {
                            if (task_info->swap_buffer_offset + LOG_HEAD_SIZE + MAX_LOG_ITEM_SIZE <= SWAP_BUFFER_SIZE) {
                                spin_lock(&task_info->swap_buffer_lock);
                                packed_logitem_in_fixed_length(task_info, pos);
                                ++(task_info->log_file_head->items);
                                spin_unlock(&task_info->swap_buffer_lock);
                            }
                            else {
                                /* waiting for upper layer obtain data */
                                // KeWaitForSingleObject(&WaitGetDataEvent, Executive, KernelMode, FALSE, &TimeoutInterval);
                            }
                        } while (pos->residue_io_data_len > 0 && ((task_info->status & (CDP_STATE_STARTED)) != 0));

                        if ((task_info->status & (CDP_STATE_PAUSE)) != 0) {
                            /*
                            * current task has been pause, preserve pupdate_bitmap and wait for resume.
                            */
                            spin_lock(&task_info->monitor_info[pos->volume_index].pupdate_bitmap->cdp_bitmap_lock);
                            ret = set_update_bitmap (task_info->monitor_info[pos->volume_index].pupdate_bitmap, 
                                                    pos->item_head.data_offset, 
                                                    pos->item_head.data_length,
                                                    1);
                            spin_unlock(&task_info->monitor_info[pos->volume_index].pupdate_bitmap->cdp_bitmap_lock);

                            if (-1 == ret) {
                                /* set pupdate_bitmap failed, current pupdate_bitmap not available */
                                if (1 == task_info->monitor_info[pos->volume_index].pupdate_bitmap->available_flag) {
                                    task_info->monitor_info[pos->volume_index].pupdate_bitmap->available_flag = 0;
                                }
                            }
                        }

                        item_len = 0;
                    }
                    else if (CDP_MIRROR_MODE == task_info->work_mode) {
                        /* do not support this mode in current version */
                    }
                    else if (CDP_BITMAP_MODE == task_info->work_mode) {
                        /*
                        * CDP_BITMAP_MODE mode use update bitmap, comprises the following steps: 
                        * 1.marking data update region on update bitmap;
                        * 2.waiting for upper layer inform synchronization data;
                        */
                        spin_lock(&task_info->monitor_info[pos->volume_index].pupdate_bitmap->cdp_bitmap_lock);
                        ret = set_update_bitmap (task_info->monitor_info[pos->volume_index].pupdate_bitmap, 
                                                pos->item_head.data_offset, 
                                                pos->item_head.data_length,
                                                1);
                        spin_unlock(&task_info->monitor_info[pos->volume_index].pupdate_bitmap->cdp_bitmap_lock);

                        if (-1 == ret) {
                            /* set pupdate_bitmap failed, current pupdate_bitmap not available */
                            if (1 == task_info->monitor_info[pos->volume_index].pupdate_bitmap->available_flag) {
                                task_info->monitor_info[pos->volume_index].pupdate_bitmap->available_flag = 0;
                            }
                        }

                        item_len = 0;
                    }
                }
                if (kthread_should_stop()) {
                    // clear tmp_io_list
                    IO_DEBUG("(%d) kthread_should_stop io: %p,  pos: %p\n", __LINE__, &pos->list, pos);
                    cdp_filter_del_tmp_io_list(&tmp_io_list);
                    break;
                }

                list_del(&pos->list);
                IO_DEBUG("(%d) break while item_len %u \n ", __LINE__, item_len);
                if(pos->data != NULL) {
                    IO_DEBUG("(%d) break while data_length %d \n ", __LINE__, pos->item_head.data_length);
                    pos->item_head.data_length <= PAGE_SIZE
                        ? kfree(pos->data)
                        : free_pages((unsigned long)pos->data, get_order(pos->item_head.data_length));
                                        pos->data = NULL;
                }

                if (pos != NULL) {
                    kfree(pos);
                    pos = NULL;
                }
            }

            // wait sync interval
            IO_DEBUG("(%d) wait sync interval %d seconds.\n", __LINE__, task_info->src_limit.sync_limit);
            temp_time = task_info->src_limit.sync_limit;
            while (!kthread_should_stop() && temp_time--) {
                ssleep(1);
            }
        } while ( !kthread_should_stop() );
        kfree(send_buf);
io_thread_exit:
        /*
        * thread terminated.
        */
        task_info->thread_state = 0;
        kernel_netlink_data_notify(task_info);
        if (sock) {
            kernel_sock_shutdown(sock, SHUT_RDWR);
            sock_release(sock);
            sock = NULL;
        }
        IO_MSG("(%d) io thread exit <task id: %d>.\n", __LINE__, task_info->task_id);
    }

    return 0;
}

/**
 * bio notify thread.
 */
static int
cdp_filter_event_notifier(void* task)
{
    struct cdp_filter_task_info *task_info = task;

    if (task == NULL) {
        IO_ERROR("(%d) cdp_filter_event_notifier parameter is NULL.\n", __LINE__);
        return 0;
    }

    do {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(28*1*HZ);

        if(!cdp_filter_task_is_alive(task_info) ) {
            if ( kthread_should_stop() ) {
                IO_ERROR("(%d) stop notiry thread.\n", __LINE__);
                break;
            }
            continue;
        }

        if ( kthread_should_stop() ) 
            break;

        if ( NULL != task_info && task_info->need_update 
            && task_info->item_cnt > task_info->item_cnt_old 
            && task_info->item_cnt != (LOG_ITEMS_PER_FILE) ) {
            IO_DEBUG("(%d) succeed to invoke timeout update, task id: %d, file name: %s, old items: %d, items: %d.\n", 
                     __LINE__,task_info->task_id, task_info->log_file_name, 
                     task_info->item_cnt_old, task_info->item_cnt);

            kernel_netlink_data_notify(task_info);
        } 
    } while ( !kthread_should_stop() );

    IO_MSG("(%d) notify thread exit <task id: %d>.\n", __LINE__, task_info->task_id);
    return 0;
}


/**
 * get job status by task id
 */
static void
status_cdp_task(struct cdp_filter_startup_request *start_request, struct nlmsghdr *hdr)
{
    uint task_id = 0;
    IO_MSG("(%d) status_cdp_task <task id: %d>.\n", __LINE__, start_request->task_id);
    if ( NULL != start_request && NULL != hdr ) {
        task_id = start_request->task_id;
        if ( (MAX_TASK_NUM) > task_id && __task_list[task_id] ) {
            IO_MSG("(%d) task_id:%d status:%d\n", __LINE__, task_id, __task_list[task_id]->status) ;
            kernel_netlink_response(__netlink_sk, CDP_CMD_STARTUP, task_id, hdr->nlmsg_pid, __task_list[task_id]->status);
            return ;
        }
    }
    kernel_netlink_response(__netlink_sk, CDP_CMD_STARTUP, task_id, hdr->nlmsg_pid, CDP_STATE_NOINIT);

}


/**
 * set monitoring flag and hook make_request_fn
 */
static void
start_cdp_task(struct cdp_filter_startup_request *start_request, struct nlmsghdr *hdr)
{
    uint i;
    uint task_id;
    dev_t dev_no;
    int slot = -1;
    long err_no = 0;

    if ( NULL != start_request && NULL != hdr ) {
          task_id = start_request->task_id;
          IO_MSG("(%d) cdp task start:task:%d.\n", __LINE__, task_id);

          if ( (MAX_TASK_NUM) > task_id && __task_list[task_id] ) {
            IO_MSG("(%d) cdp task start, create io filter thread, task_id is: %d, pid is: %d.\n", 
                   __LINE__, __task_list[task_id]->task_id, __task_list[task_id]->pid);

            __task_list[task_id]->io_thread = kthread_run(cdp_filter_thread_handler, 
                                                          __task_list[task_id], 
                                                          "cdp_filter_data_thread_%d_%d", 
                                                          __task_list[task_id]->task_id, 
                                                          __task_list[task_id]->pid);
            if ( IS_ERR(__task_list[task_id]->io_thread) ) {
                IO_ERROR("(%d) start task, failed to create io filter thread, the ERROR CODE is: %ld.\n", 
                         __LINE__, err_no = PTR_ERR(__task_list[task_id]->io_thread));
                goto start_exit;
            }

            if (CDP_CACHED_MODE == __task_list[task_id]->work_mode) {
                IO_MSG("(%d) create event notification thread, task_id is: %d, pid is: %d.\n", 
                       __LINE__, __task_list[task_id]->task_id, __task_list[task_id]->pid);

                __task_list[task_id]->notify_thread = kthread_run(cdp_filter_event_notifier, 
                                                                  __task_list[task_id], 
                                                                  "cdp_filter_notification_thread_%d_%d", 
                                                                  __task_list[task_id]->task_id, 
                                                                  __task_list[task_id]->pid);
                if ( IS_ERR(__task_list[task_id]->notify_thread) ) {
                    IO_ERROR("(%d) start task, failed to create event notification thread, the ERROR CODE is: %ld.\n", 
                             __LINE__, err_no = PTR_ERR(__task_list[task_id]->notify_thread));
                    goto start_exit;
                }
            }

            __task_list[task_id]->flag = true;
        }

        for ( i = 0; i < __task_list[task_id]->monitor_infos; ++i ) {
            dev_no = __task_list[task_id]->monitor_info[i].dev_no;
            slot = cdp_filter_calc_dev_slot(MAJOR(dev_no), MINOR(dev_no));
            if ( slot < 0  || NULL == __dev_list[slot] ) {
                IO_ERROR("(%d) start task, failed to find device object for <%d>, slot is: %d, the ERROR CODE is: %ld.\n", 
                         __LINE__, dev_no, slot, err_no = -EINVAL);
                goto start_exit;
            }
            else {
                IO_DEBUG("(%d) __dev_list[slot]->flag == CDP_DISK_NON_MONITOR %d.\n", 
                         __LINE__, __dev_list[slot]->flag == CDP_DISK_NON_MONITOR );

                if ( __dev_list[slot]->flag == CDP_DISK_NON_MONITOR ) {
                    IO_DEBUG("(%d) succeed to hook device num <%d> specified device, slot is: %d.\n",  __LINE__, dev_no, slot);
                             __dev_list[slot]->flag = CDP_DISK_IN_MONITOR;

                    if(__dev_list[slot]->disk == NULL) {
                        IO_ERROR("(%d) start task, disk obj is null.\n", __LINE__);
                        err_no = -CDP_DISK_STATUS_UNKNOWN;
                    }
                    else if(__dev_list[slot]->disk->queue == NULL) {
                        IO_ERROR("(%d) start task, disk queue obj is null.\n", __LINE__);
                        err_no = -ERROR_QUEUE_NULL;
                    }
                    else if (__dev_list[slot]->disk->queue->make_request_fn == NULL){
                        IO_ERROR("(%d) start task, disk make_request_fn is null.\n", __LINE__);
                        err_no = -ERROR_MAKE_FN_NULL;
                    }
                    else {
                        __dev_list[slot]->disk->queue->make_request_fn = cdp_filter_make_request;
                        __task_list[task_id]->status = CDP_STATE_STARTED;
                    }
                }
            }
        }

start_exit:
        kernel_netlink_response(__netlink_sk, CDP_CMD_STARTUP, task_id, hdr->nlmsg_pid, err_no);
    }
}

/**
 * re start task, 
 * notes: upper layer update monitor volumes will call this function.
 */
static void
restart_cdp_task(struct cdp_filter_startup_request *start_request, struct nlmsghdr *hdr)
{
    uint i, j;
    uint task_id;
    dev_t dev_no;
    int slot = -1;
    long err_no = 0;

    IO_MSG("(%d) task restart.\n", __LINE__);

    if ( NULL != start_request && NULL != hdr ) {
        task_id = start_request->task_id;

        if ( (MAX_TASK_NUM) <= task_id || NULL == __task_list[task_id] ) {
            IO_ERROR("(%d) task restart, task id is out of boundary or task is NULL.\n", __LINE__);
            err_no = -EINVAL;
            goto restart_exit;
        }

        for ( i = __task_list[task_id]->monitor_infos - __task_list[task_id]->monitor_infos_inc, j = 0; 
              j < __task_list[task_id]->monitor_infos_inc; 
              ++i, ++j ) {
            dev_no = __task_list[task_id]->monitor_info[i].dev_no;
            slot = cdp_filter_calc_dev_slot(MAJOR(dev_no), MINOR(dev_no));
            if ( slot < 0  || NULL == __dev_list[slot] ) {
                IO_ERROR("(%d) task restart, failed to find device object for <%d>, slot is: %d, the ERROR CODE is: %ld.\n", 
                         __LINE__, dev_no, slot, err_no = -EINVAL);
                goto restart_exit;
            }
            else {
                if ( __dev_list[slot]->flag == (CDP_DISK_NON_MONITOR) ) {
                    IO_DEBUG("(%d) succeed to hook device num <%d> specified device, slot is: %d.\n", __LINE__, dev_no, slot);
                    __dev_list[slot]->flag = CDP_DISK_IN_MONITOR;
                    if(__dev_list[slot]->disk == NULL) {
                        IO_ERROR("(%d) task restart, disk obj is null.\n", __LINE__);
                        err_no = -CDP_DISK_STATUS_UNKNOWN;
                    }
                    else if(__dev_list[slot]->disk->queue == NULL) {
                        IO_ERROR("(%d) task restart, disk queue obj is null.\n", __LINE__);
                        err_no = -ERROR_QUEUE_NULL;
                    }
                    else if (__dev_list[slot]->disk->queue->make_request_fn == NULL){
                        IO_ERROR("(%d) task restart, disk make_request_fn is null.\n", __LINE__);
                        err_no = -ERROR_MAKE_FN_NULL;
                    }
                    else {
                        __dev_list[slot]->disk->queue->make_request_fn = cdp_filter_make_request;
                    }
                }
            }
        }

restart_exit:
        __task_list[task_id]->monitor_infos_inc = 0;
        kernel_netlink_response(__netlink_sk, CDP_CMD_RESTART, task_id, hdr->nlmsg_pid, err_no);
    }
}

/**
 * delete_cdp_task - handle CDP_CMD_DEL request.
 * @del_request: delete task request packet from upper layer.
 * @hdr: 
 */
static void
delete_cdp_task(cdp_filter_del_stop_request *del_request, struct nlmsghdr *hdr)
{
    uint task_id;
    IO_MSG("(%d) response CDP_CMD_DEL begin.\n", __LINE__);

    if ( NULL != del_request && NULL != hdr ) {
        task_id = del_request->task_id;
        cdp_filter_clear_device_list();
        cdp_filter_clear_task_by_id(task_id);
        kernel_netlink_response(__netlink_sk, CDP_CMD_DEL, task_id, hdr->nlmsg_pid, 0);
        __task_list[task_id]->status = CDP_STATE_NOINIT;
    }

    IO_MSG("(%d) response CDP_CMD_DEL end.\n", __LINE__);
}

/**
 * stop_cdp_task - handle CDP_CMD_STOP request,
 *               - clear specified task for task list and inform upper layer.
 * @stop_request: stop task request packet from upper layer.
 * @hdr: 
 */
static void
stop_cdp_task(cdp_filter_del_stop_request *stop_request, struct nlmsghdr *hdr)
{
    uint task_id;
    IO_MSG("(%d) response CDP_CMD_STOP begin.\n", __LINE__);

    if ( NULL != stop_request && NULL != hdr ) {
        task_id = stop_request->task_id;
        cdp_filter_del_task_by_id(task_id);
        kernel_netlink_response(__netlink_sk, CDP_CMD_STOP, task_id, hdr->nlmsg_pid, 0);
    }

    IO_MSG("(%d) response CDP_CMD_STOP end.\n", __LINE__);
}

static void 
cdp_filter_set_consistent_flag(cdp_filter_consistent_flag_request *flag_request, struct nlmsghdr *hdr)
{
    uint task_id;
    struct timeval tv;
    struct cdp_filter_data_info *data_info;

    if(flag_request == NULL || hdr == NULL) {
        IO_ERROR("(%d) failed to set consistent flag, parameter is null.\n", __LINE__);
        return;
    }

    task_id = flag_request->task_id;
    data_info = kmalloc(sizeof(struct cdp_filter_data_info), GFP_KERNEL);
    if ( IS_ERR(data_info) ) {
        IO_ERROR("(%d) failed to allocate memory, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(data_info));
        return;
    }

    /* construct cdp_filter_data_info with consistent flag, not really io data */
    data_info->data = NULL;
    data_info->item_head.data_length = 1;
    data_info->item_head.data_offset = 0;
    do_gettimeofday (&tv);
    data_info->item_head.io_timestamp = ((__u64)tv.tv_sec * USEC_PER_SEC + tv.tv_usec);
    data_info->item_head.item_type = DATA_TYPE_LABEL;

    memset(data_info->item_head.mount_point, 0, DISK_DOS_NAME_LEN);

    spin_lock(&__task_list[task_id]->io_list_lock);
    list_add_tail(&data_info->list, &__task_list[task_id]->io_list);
    spin_unlock(&__task_list[task_id]->io_list_lock);
    up(&__task_list[task_id]->io_list_sem);

    kernel_netlink_response(__netlink_sk, CDP_CMD_CONSIS, task_id, __task_list[task_id]->pid, 0);

    IO_MSG("(%d) succeed to set consistent flag.\n", __LINE__);
}

static void 
cdp_filter_set_bitmap(cdp_filter_set_bitmap_request *set_bitmap_request, struct nlmsghdr *hdr)
{
    uint i;
    uint task_id;

    if(set_bitmap_request == NULL || hdr == NULL) {
        IO_ERROR("(%d) failed to set bitmap, parameter is null.\n", __LINE__);
        return;
    }

    task_id = set_bitmap_request->task_id;

    for (i = 0; i < __task_list[task_id]->monitor_infos; ++i) {
        spin_lock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
        swap_update_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
        spin_unlock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
    }

    if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
        __task_list[task_id]->success_last_time = false;
    }

    kernel_netlink_response(__netlink_sk, CDP_CMD_SET_BITMAP, task_id, __task_list[task_id]->pid, 0);

    IO_MSG("(%d) succeed to set bitmap.\n", __LINE__);
}

static void 
cdp_filter_reset_bitmap(cdp_filter_reset_bitmap_request *reset_bitmap_request, struct nlmsghdr *hdr)
{
    uint i;
    uint task_id;

    if(reset_bitmap_request == NULL || hdr == NULL) {
        IO_ERROR("(%d) failed to reset bitmap, parameter is null.\n", __LINE__);
        return;
    }

    task_id = reset_bitmap_request->task_id;

    for (i = 0; i < __task_list[task_id]->monitor_infos; ++i) {
        spin_lock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
        reset_update_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
        spin_unlock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
    }

    if (CDP_BITMAP_MODE == __task_list[task_id]->work_mode) {
        __task_list[task_id]->success_last_time = true;
    }

    kernel_netlink_response(__netlink_sk, CDP_CMD_RESET_BITMAP, task_id, __task_list[task_id]->pid, 0);

    IO_MSG("(%d) succeed to reset bitmap.\n", __LINE__);
}

static void 
cdp_filter_merge_bitmap(cdp_filter_merge_bitmap_request *merge_bitmap_request, struct nlmsghdr *hdr)
{
    uint i;
    uint task_id;

    if(merge_bitmap_request == NULL || hdr == NULL) {
        IO_ERROR("(%d) failed to merge bitmap, parameter is null.\n", __LINE__);
        return;
    }

    task_id = merge_bitmap_request->task_id;

    for (i = 0; i < __task_list[task_id]->monitor_infos; ++i) {
        spin_lock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
        merge_update_bitmap(__task_list[task_id]->monitor_info[i].pupdate_bitmap);
        spin_unlock(&__task_list[task_id]->monitor_info[i].pupdate_bitmap->cdp_bitmap_lock);
    }

    kernel_netlink_response(__netlink_sk, CDP_CMD_MERGE_BITMAP, task_id, __task_list[task_id]->pid, 0);

    IO_MSG("(%d) succeed to merge bitmap.\n", __LINE__);
}

static void 
cdp_filter_get_bitmap(cdp_filter_bitmap_request_user *bitmap_request_user, struct nlmsghdr *hdr)
{
    __u64 offset = 0;      // the offset of this io.
    __u32 length = 0;      // the length of this io.
    uint io_metadata_count = 0;
    uint task_id;
    uint volume_id;

    if(bitmap_request_user == NULL || hdr == NULL) {
        IO_ERROR("(%d) failed to merge bitmap, parameter is null.\n", __LINE__);
        return;
    }

    task_id = bitmap_request_user->task_id;
    volume_id = bitmap_request_user->volume_Id;

    if (CDP_BITMAP_MODE != __task_list[task_id]->work_mode) {
        IO_ERROR("(%d) failed to get bitmap, cdp_filter work mode is incorrect.\n", __LINE__);
        return;
    }

    memset(__task_list[task_id]->io_metadata_entries, 0, sizeof(struct cdp_filter_io_metadata)*MAX_IO_METADATA_ENTRIES);

    while (io_metadata_count < MAX_IO_METADATA_ENTRIES) {
        seek_update_bitmap (__task_list[task_id]->monitor_info[volume_id].pupdate_bitmap, &offset, &length);

        IO_DEBUG("(%d) seek io metadata info, offset: %lld, length: %u.\n", __LINE__, offset, length);

        if ( 0 != length ) {
            /* get a io metadata entry mark by update bitmap */
            __task_list[task_id]->io_metadata_entries[io_metadata_count].offset = offset;
            __task_list[task_id]->io_metadata_entries[io_metadata_count].length = length;
        }
        else {
            /* already get all io metadata mark by update bitmap */
            break;
        }

        ++io_metadata_count;
    }

    kernel_netlink_response(__netlink_sk, CDP_CMD_GET_BITMAP, task_id, __task_list[task_id]->pid, 0);

    IO_MSG("(%d) succeed to get bitmap.\n", __LINE__);
}

static void 
cdp_filter_netlink_cmd_dealer(struct sk_buff *skb)
{
    struct nlmsghdr *hdr = (struct nlmsghdr*)skb->data;

    IO_MSG(" (%d) Received message: %d\n",__LINE__, hdr->nlmsg_type);

    switch ( hdr->nlmsg_type ) {
        case CDP_CMD_INIT:          /*0x00000001*/
            {
                struct cdp_filter_init_request *init_request = (struct cdp_filter_init_request*)skb->data;
                init_cdp_task(init_request, hdr);
			//	connect_send_recv(init_request);
            }
            break;
        case CDP_CMD_REINIT:        /*0x00000007*/
            {
                struct cdp_filter_init_request *init_request = (struct cdp_filter_init_request*)skb->data;
                reinit_cdp_task(init_request, hdr);
            }
            break;
        case CDP_CMD_STARTUP:       /*0x00000002*/
            {
                struct cdp_filter_startup_request *start_request = (struct cdp_filter_startup_request*)skb->data;
                start_cdp_task(start_request, hdr);
            }
            break;
        case CDP_CMD_STATUS:       /*0x00000014*/
            {
                struct cdp_filter_startup_request *start_request = (struct cdp_filter_startup_request*)skb->data;
                status_cdp_task(start_request, hdr);
            }
            break;
        case CDP_CMD_RESTART:       /*0x00000008*/
            {
                struct cdp_filter_startup_request *start_request = (struct cdp_filter_startup_request*)skb->data;
                restart_cdp_task(start_request, hdr);
            }
            break;
        case CDP_CMD_DATA:          /*0x00000003*/
            {
                //no-ops
            }
            break;
        case CDP_CMD_DEL:           /*0x00000004*/
            {
                cdp_filter_del_stop_request *del_request = (cdp_filter_del_stop_request*)skb->data;
                delete_cdp_task(del_request, hdr);
            }
            break;
        case CDP_CMD_STOP:          /*0x00000005*/
            {
                cdp_filter_del_stop_request *stop_request = (cdp_filter_del_stop_request*)skb->data;
                stop_cdp_task(stop_request, hdr);
            }
            break;
        case CDP_CMD_CONSIS:        /*0x0000000A*/
            {            
                cdp_filter_consistent_flag_request *flag_request = (cdp_filter_consistent_flag_request*)skb->data;
                cdp_filter_set_consistent_flag(flag_request, hdr);
            }
            break;
        case CDP_CMD_SET_BITMAP:    /*0x00000010*/
            {
                cdp_filter_set_bitmap_request *set_bitmap_request = (cdp_filter_set_bitmap_request*)skb->data;
                cdp_filter_set_bitmap(set_bitmap_request, hdr);
            }
            break;
        case CDP_CMD_RESET_BITMAP:  /*0x00000011*/
            {
                cdp_filter_reset_bitmap_request *reset_bitmap_request = (cdp_filter_reset_bitmap_request*)skb->data;
                cdp_filter_reset_bitmap(reset_bitmap_request, hdr);
            }
            break;
        case CDP_CMD_MERGE_BITMAP:  /*0x00000012*/
            {
                cdp_filter_merge_bitmap_request *merge_bitmap_request = (cdp_filter_merge_bitmap_request*)skb->data;
                cdp_filter_merge_bitmap(merge_bitmap_request, hdr);
            }
            break;
        case CDP_CMD_GET_BITMAP:    /*0x00000013*/
            {
                cdp_filter_bitmap_request_user *bitmap_request_user = (cdp_filter_bitmap_request_user*)skb->data;
                cdp_filter_get_bitmap(bitmap_request_user, hdr);
            }
            break;
        default:
            IO_WARN("(%d) failed to recognise this cdp request, request type is: %d.\n", __LINE__, hdr->nlmsg_type);
    }
}

#if defined(k_4_x_x) || defined(k_3_10_0) || defined(k_2_6_24) || defined(k_2_6_26) || defined(k_2_6_32)
static void 
kernel_netlink_dealer(struct sk_buff *skb)
{
    IO_DEBUG("(%d) kernel_netlink_dealer.\n", __LINE__);
    IO_MSG("(%d) kernel_netlink_dealer.\n", __LINE__);

    if ( NULL != skb && skb->len > sizeof(struct nlmsghdr) ) {
        cdp_filter_netlink_cmd_dealer(skb);
    }
}

static void
kernel_netlink_data_dealer(struct sk_buff *skb) 
{
    //no-ops
}

#elif defined(k_2_6_9) || defined(k_2_6_13) || defined(k_2_6_16) || defined(k_2_6_18)
static void
kernel_netlink_dealer(struct sock *sk, int len)
{
    struct sk_buff *skb;

    do {
        if (sk == NULL) {
            IO_ERROR("(%d) kernel_netlink_dealer parameter is null error.\n", __LINE__);
            return;
        }

        /*
         * try to get the semaphore.
         */
        if ( down_trylock(&__netlink_sem) )
            return ;

        while ( NULL != (skb = skb_dequeue(&sk->sk_receive_queue)) && skb->len > sizeof(struct nlmsghdr) ) {
            cdp_filter_netlink_cmd_dealer(skb);

            kfree_skb(skb);
            skb = NULL;
        }

        /*
         * release receive lock.
         */
        up(&__netlink_sem);

    } while ( __netlink_sk && __netlink_sk->sk_receive_queue.qlen );
}

static void
kernel_netlink_data_dealer(struct sock *sk, int len) 
{
    //no-ops
}

#else
#endif

static int 
cdp_filter_create_netlink_sk (void)
{
    int linkUnit = 28;

    do {
#if defined (k_2_6_9) || defined (k_2_6_13)
        __netlink_sk = netlink_kernel_create(linkUnit, kernel_netlink_dealer);
#elif defined(k_2_6_16) || defined (k_2_6_18)
        __netlink_sk = netlink_kernel_create(linkUnit, 0, kernel_netlink_dealer, THIS_MODULE);
#elif defined (k_2_6_24) || defined(k_2_6_26) || defined(k_2_6_32)
        __netlink_sk = netlink_kernel_create(&init_net, linkUnit, 0, kernel_netlink_dealer, 0, THIS_MODULE);
#elif defined(k_4_x_x) || defined(k_3_10_0) 
        struct netlink_kernel_cfg netlinkCfg;
        memset(&netlinkCfg, 0, sizeof(struct netlink_kernel_cfg));
        netlinkCfg.input = kernel_netlink_dealer;
        
        __netlink_sk = netlink_kernel_create(&init_net, linkUnit, &netlinkCfg);
#endif

        if (__netlink_sk != NULL) {
            return linkUnit;
        }
    }while (--linkUnit > 19);

    return -1;
}

static int 
cdp_filter_create_data_sk (void)
{
    int linkUnit = 28;

    do {
#if defined (k_2_6_9) || defined (k_2_6_13)
        __netlink_data_sk = netlink_kernel_create(linkUnit, kernel_netlink_data_dealer);
#elif defined(k_2_6_16) || defined (k_2_6_18)
        __netlink_data_sk = netlink_kernel_create(linkUnit, 0, kernel_netlink_data_dealer, THIS_MODULE);
#elif defined (k_2_6_24) || defined(k_2_6_26) || defined(k_2_6_32)
        __netlink_data_sk = netlink_kernel_create(&init_net, linkUnit, 0, kernel_netlink_data_dealer, 0, THIS_MODULE);
#elif defined(k_4_x_x) || defined(k_3_10_0) 
        struct netlink_kernel_cfg netlinkCfg;
        memset(&netlinkCfg, 0, sizeof(struct netlink_kernel_cfg));
        netlinkCfg.input = kernel_netlink_dealer;
        
        __netlink_data_sk = netlink_kernel_create(&init_net, linkUnit, &netlinkCfg);
#endif

        if (__netlink_data_sk != NULL) {
            return linkUnit;
        }
    }while (--linkUnit > 19);

    return -1;
}

static int 
cdp_filter_create_excep_sk (void)
{
    int linkUnit = 28;

    do {
#if defined (k_2_6_9) || defined (k_2_6_13)
        __netlink_excep_sk = netlink_kernel_create(linkUnit, kernel_netlink_data_dealer);
#elif defined(k_2_6_16) || defined (k_2_6_18)
        __netlink_excep_sk = netlink_kernel_create(linkUnit, 0, kernel_netlink_data_dealer, THIS_MODULE);
#elif defined (k_2_6_24) || defined(k_2_6_26) || defined(k_2_6_32)
        __netlink_excep_sk = netlink_kernel_create(&init_net, linkUnit, 0, kernel_netlink_data_dealer, 0, THIS_MODULE);
#elif defined(k_4_x_x) || defined(k_3_10_0) 
        struct netlink_kernel_cfg netlinkCfg;
        memset(&netlinkCfg, 0, sizeof(struct netlink_kernel_cfg));
        netlinkCfg.input = kernel_netlink_dealer;
        
        __netlink_excep_sk = netlink_kernel_create(&init_net, linkUnit, &netlinkCfg);
#endif
        if (__netlink_excep_sk != NULL) {
            return linkUnit;
        }
    }while (--linkUnit > 19);

    return -1;
}

static int
write_netlink_type_log_file(int netlink_sk_type, int data_sk_type, int excp_sk_type)
{
    char buf [50];
    int ret = 0;
    mm_segment_t old_fs;
    ssize_t vfs_ret;
    struct file *netlink_file_handle = filp_open("/var/log/.netlink.log", O_CREAT|O_RDWR, S_IRWXU|S_IRWXG);
    if ( IS_ERR(netlink_file_handle) ) {
        IO_ERROR("(%d) failed to creat netlink log file, the ERROR CODE is: %ld.\n",
                 __LINE__, PTR_ERR(netlink_file_handle));
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    snprintf(buf, 50, "netlink:%d;datalink:%d;excplink:%d", 
             netlink_sk_type, data_sk_type, excp_sk_type);

    netlink_file_handle->f_pos = 0;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_ret = vfs_write(netlink_file_handle, buf, 50, &netlink_file_handle->f_pos);
    set_fs(old_fs);

    if(vfs_ret < 50) {
        IO_ERROR("(%d) vfs write file head error??return value:%u.\n", __LINE__,(unsigned int)vfs_ret); 
        ret = -1;
    }

    if ( NULL != netlink_file_handle ) {
        filp_close(netlink_file_handle, NULL);
        netlink_file_handle = NULL;
    }

    return ret;
}

static int __init
cdp_filter_init(void)
{
    int netlinksk = CDP_NETLINK_TYPE;
    int datalinksk = CDP_DATA_NETLINK;
    int excplinksk = CDP_EXCP_NETLINK;
    int ret = 0;
#if defined(k_4_x_x) || defined(k_3_10_0) 
    struct netlink_kernel_cfg netlinkCfg;
#endif

    IO_MSG("(%d) io filter init.\n", __LINE__);

    /*
     * initialize global lists.
     */
    task_list_init();
    dev_list_init();

    /*
     * create netlink socket.
     */
#if defined (k_2_6_9) || defined (k_2_6_13)
    __netlink_sk = netlink_kernel_create(CDP_NETLINK_TYPE, kernel_netlink_dealer);
    __netlink_data_sk = netlink_kernel_create(CDP_DATA_NETLINK, kernel_netlink_data_dealer);
    __netlink_excep_sk = netlink_kernel_create(CDP_EXCP_NETLINK, kernel_netlink_data_dealer);
#elif defined(k_2_6_16) || defined (k_2_6_18)
    __netlink_sk = netlink_kernel_create(CDP_NETLINK_TYPE, 0, kernel_netlink_dealer, THIS_MODULE);
    __netlink_data_sk = netlink_kernel_create(CDP_DATA_NETLINK, 0, kernel_netlink_data_dealer, THIS_MODULE);
    __netlink_excep_sk = netlink_kernel_create(CDP_EXCP_NETLINK, 0, kernel_netlink_data_dealer, THIS_MODULE);
#elif defined (k_2_6_24) || defined(k_2_6_26) || defined(k_2_6_32)
    __netlink_sk = netlink_kernel_create(&init_net, CDP_NETLINK_TYPE, 0, kernel_netlink_dealer, 0, THIS_MODULE);
    __netlink_data_sk = netlink_kernel_create(&init_net, CDP_DATA_NETLINK, 0, kernel_netlink_data_dealer, 0, THIS_MODULE);
    __netlink_excep_sk = netlink_kernel_create(&init_net, CDP_EXCP_NETLINK, 0, kernel_netlink_data_dealer, 0, THIS_MODULE);
#elif defined(k_4_x_x) || defined(k_3_10_0)
    memset(&netlinkCfg, 0, sizeof(struct netlink_kernel_cfg));
    netlinkCfg.input = kernel_netlink_dealer;
    
    __netlink_sk = netlink_kernel_create(&init_net, CDP_NETLINK_TYPE, &netlinkCfg);
    
    netlinkCfg.input = kernel_netlink_data_dealer;
    __netlink_data_sk = netlink_kernel_create(&init_net, CDP_DATA_NETLINK, &netlinkCfg);
    __netlink_excep_sk = netlink_kernel_create(&init_net, CDP_EXCP_NETLINK, &netlinkCfg);
#else
    __netlink_sk = 0;
    __netlink_data_sk = 0;
    __netlink_excep_sk = 0;
    IO_ERROR("(%d) failed to create netlink socket, the reason is: un-supported platform, <%d>.\n", __LINE__, LINUX_VERSION_CODE);
    return -1;
#endif


    if (0 != __netlink_sk && 0 != __netlink_data_sk && 0 != __netlink_excep_sk) {
        /*create netlink with expected type succeed.*/
        return ret;
    }

    /*
     * failed to create netlink with expected type, need recreate.
     */
    if ( 0 == __netlink_sk ) {
        netlinksk = cdp_filter_create_netlink_sk ();
        IO_MSG("(%d) first failed to create netlink socket, netlink type: %d.\n", __LINE__, netlinksk);
        if (0 == __netlink_sk) {
            IO_ERROR("(%d) failed to create netlink socket, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(__netlink_sk));
            ret = -1;
            goto init_exit;
        }
    }

    if ( 0 == __netlink_data_sk ) {
        datalinksk = cdp_filter_create_data_sk ();
        IO_MSG("(%d) first failed to create data type netlink socket, netlink type:%d.\n", __LINE__, datalinksk);
        if (0 == __netlink_data_sk) {
            IO_ERROR("(%d) failed to create netlink socket, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(__netlink_sk));
            ret = -1;
            goto init_exit;
        }
    }

    if ( 0 == __netlink_excep_sk ) {
        excplinksk = cdp_filter_create_excep_sk ();
        IO_ERROR("(%d) first failed to create netlink portal for exception handler, netlink type:%d\n", __LINE__, excplinksk);
        if (0 == __netlink_excep_sk) {
            IO_ERROR("(%d) failed to create netlink socket, the ERROR CODE is: %ld.\n", __LINE__, PTR_ERR(__netlink_sk));
            ret = -1;
            goto init_exit;
        }
    }

init_exit:
    if (ret != -1) {
        /*create netlink with unexpected type, need inform upper layer by promissory file.*/
        ret = write_netlink_type_log_file(netlinksk, datalinksk, excplinksk);
    }
    else {
        /*this is really a pity, failed to create netlink type.*/
        if ( __netlink_sk ) {
            sock_release(__netlink_sk->sk_socket);
            __netlink_sk = NULL;
            IO_MSG("(%d) release netlink_sk.\n", __LINE__);
        }

        if ( __netlink_data_sk ) {
            sock_release(__netlink_data_sk->sk_socket);
            __netlink_data_sk = NULL;
            IO_MSG("(%d) release netlink_sk_data.\n", __LINE__);
        }

        if ( __netlink_excep_sk ) {
            sock_release(__netlink_excep_sk->sk_socket);
            __netlink_excep_sk = NULL;
            IO_MSG("(%d) release netlink_sk_excep.\n", __LINE__);
        }
    }

    return ret;
}

static void __exit
cdp_filter_exit(void)
{
    IO_MSG("(%d) io filter exits.\n", __LINE__);


	if(sock)
	{
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
        sock = NULL;
	}
	
	cdp_filter_del_task_list();

    if ( __netlink_sk ) {
        sock_release(__netlink_sk->sk_socket);
        __netlink_sk = NULL;
        IO_MSG("(%d) cdp_filter_exit release netlink_sk.\n", __LINE__);
    }

    if ( __netlink_data_sk ) {
        sock_release(__netlink_data_sk->sk_socket);
        __netlink_data_sk = NULL;
        IO_MSG("(%d) cdp_filter_exit release netlink_sk_data.\n", __LINE__);
    }

    if ( __netlink_excep_sk ) {
        sock_release(__netlink_excep_sk->sk_socket);
        __netlink_excep_sk = NULL;
        IO_MSG("(%d) cdp_filter_exit release netlink_sk_excep.\n", __LINE__);
    }

    IO_MSG("(%d) cdp_filter successfully exits.\n", __LINE__);
}

module_init(cdp_filter_init);
module_exit(cdp_filter_exit);

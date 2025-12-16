#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/socket.h>
#include <errno.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <mntent.h>
#define MAX_PAYLOAD 1024 // maximum payload size
#define NETLINK_TEST 30 // 自定义的协议
#define DISK_DOS_NAME_LEN   (64)


struct io_filter_jbd_range {
    __u64 start_offset;     // the start offset of JBD.
    __u64 end_offset;       // the end offset of JBD.
};

struct io_filter_init_info {
    __u16 major_no;                         // major device number
    __u16 minor_no;                         // minor device number
    __u32 sec_size;                         // sector size
    __u32 bl_size;                          // block size
    __u32 bl_count;                         // total block count of this monitored device
    __u8 mount_point[DISK_DOS_NAME_LEN];    // the name of mount point, e.g: /a
    __u8 dev_name[DISK_DOS_NAME_LEN];       // the name of device, e.g: /dev/sda1
    __u8 log_path[DISK_DOS_NAME_LEN];       // the name of log path, e.g: /b
    __u8 log_dev_name[DISK_DOS_NAME_LEN];   // the name of log device, e.g: /dev/sda2
    struct io_filter_jbd_range jbd_range;   // the range of JBD.
}__packed;

struct io_filter_src_limit {
    long long mem_limit;        // max available size, in GB, of memory
    long long disk_limit;       // max available size, in GB, of disk space
};

struct io_filter_init_request {
    struct nlmsghdr hdr;
    __u32 item_cnt;                         // elements contained in init_info
    __u32 work_mode;                        // the work mode of cdp driver.
	__u8 ip_addr[20];
    struct io_filter_src_limit src_limit;
    struct io_filter_init_info init_info[];
};

void get_mount_name(char* dev_name ,char* mount_name);

int main(int argc, char* argv[])
{
	int state;
	int sock_fd, retval;
	int state_smg = 0;
	int recv_smg = 0;
	int fd;
	int sector_size;
	unsigned int filter_size;

	char mount_name[64]={0};
	char *dev_name;
	struct msghdr msg;
	struct iovec iov;
	struct stat dev_st;
	struct statfs dev_fs;
	struct sockaddr_nl scr_addr, dest_addr;
	struct io_filter_init_request *io_request;

	memset(&dev_fs, 0, sizeof(struct statfs));
	memset(&dev_st, 0, sizeof(struct stat));

	dev_name = "/dev/sdc";
	if(stat(dev_name, &dev_st) < 0)
	{
		printf("stat error \n");
	}

	if(statfs(dev_name, &dev_fs) < 0)
	{
		printf("statfs error /n ");
	}

	fd = open(dev_name, O_RDONLY);
	if(ioctl(fd, BLKSSZGET, &sector_size)<0)
	{
		printf("sector error \n");
		return 0;
	}
	close (fd);

	get_mount_name(dev_name, mount_name);

	sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);

/*
	struct nlmsghdr *nlh = NULL; // Netlink数据包头
	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
*/
	filter_size = sizeof(struct io_filter_init_request) + sizeof(struct io_filter_init_info);
	io_request = (struct io_filter_init_request *)malloc(filter_size);

	memset(io_request, 0, filter_size);

	if(!io_request){
		printf("malloc nlmsghdr error!\n");
		close(sock_fd);
		return -1;
	}

	io_request->work_mode = 0;
	io_request->item_cnt = 1;
	memcpy(io_request->ip_addr, "172.18.0.124", 12);
	io_request->init_info[0].major_no = major(dev_st.st_rdev);
	printf("major_no 8 dev_st.maj %d\n", major(dev_st.st_rdev));
	io_request->init_info[0].minor_no = minor(dev_st.st_rdev);
	printf("minor_no 18 dev_st.min %d\n", minor(dev_st.st_rdev));
	io_request->init_info[0].sec_size = sector_size;
	printf("sec_size %d \n", sector_size);
	io_request->init_info[0].bl_size = dev_fs.f_bsize;
	printf("bl_size %d \n", dev_fs.f_bsize);
	io_request->init_info[0].bl_count = dev_fs.f_blocks;
	printf("bl_count %ld \n", dev_fs.f_blocks);
	memcpy(io_request->init_info[0].mount_point, mount_name, strlen(mount_name));
	printf("mount_name %s, strlen %d  \n", mount_name, strlen(mount_name));
	memcpy(io_request->init_info[0].dev_name, dev_name, strlen(dev_name));
	printf("dev_name %s strlen %d \n", dev_name, strlen(dev_name));
/*
	memcpy(io_request->init_info[0].log_path, "/home/logdisk",17);
	memcpy(io_request->init_info[0].log_dev_name, "/dev/sdb3",9);
*/

	memset(&dest_addr, 0, sizeof(dest_addr));
	memset(&scr_addr, 0, sizeof(scr_addr));

	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0; // B：设置目的端口号
	dest_addr.nl_groups = 0;
	
	scr_addr.nl_family = AF_NETLINK;
	scr_addr.nl_pid = 100;
	scr_addr.nl_groups = 0;

	retval = bind(sock_fd, (struct sockaddr*)&scr_addr, sizeof(scr_addr));
	if(retval < 0)
	{
		printf("bind failed: %s", strerror(errno));
		close(sock_fd);
		return -1;
	}

	io_request->hdr.nlmsg_len = filter_size;
	io_request->hdr.nlmsg_pid = 100; // C：设置源端口
	io_request->hdr.nlmsg_flags = 0;
	io_request->hdr.nlmsg_type = 0x00000001;
	printf("%d\n",__LINE__);

/*      
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = 100;
	nlh->nlmsg_flags = 0;
	strcpy(NLMSG_DATA(nlh), "Hello you!"); // 设置消息体
	iov.iov_base = (void *)nlh;
	iov.iov_len = NLMSG_SPACE(MAX_PAYLOAD); 
*/

	iov.iov_base = (void *)io_request;
	iov.iov_len = filter_size;

	//Create mssage
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	//send message
	printf("state_smg\n");
	state_smg = sendmsg(sock_fd,&msg,0);
	if(state_smg == -1)
	{
		printf("get error sendmsg = %s\n",strerror(errno));
	}

	printf("%d\n",__LINE__);
	memset(io_request,0,filter_size);

	recv_smg = recvmsg(sock_fd,&msg,0);
	printf("recv_smg\n");

	return 0;
}

void get_mount_name(char *dev_name , char *mount_name)
{
	FILE *fd;
	struct mntent *mnt =NULL;

	fd = setmntent("/proc/mounts", "r");

	if(fd == NULL)
	{
		printf("setmntent error \n");
		return ;
	}

	while( (mnt = getmntent(fd)) != NULL )
	{
		if(strcmp(dev_name, mnt->mnt_fsname) == 0)
		{
			break;
		}
	}

	if( mnt == NULL)
	{
		printf("NOT FOUND \n");
		return ;
	}

	strcpy(mount_name, mnt->mnt_dir);
}

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
#define MAX_PAYLOAD 1024 // maximum payload size
#define NETLINK_TEST 30 //自定义的协议
#define DISK_DOS_NAME_LEN   (64)

typedef struct io_filter_startup_request {
    struct nlmsghdr hdr;
    __u32 task_id;
} io_filter_startup_request;


int main(int argc, char* argv[])
{
		int state;
		struct sockaddr_nl scr_addr, dest_addr;
//		struct nlmsghdr *nlh = NULL; //Netlink数据包头
		struct iovec iov;
		struct msghdr msg;
		int sock_fd, retval;
		int state_smg = 0,recv_smg = 0;
        struct io_filter_startup_request io_requeset;
				
		sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);
		// To orepare create mssage
	
//		nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
        
		memset(&io_requeset,0,sizeof(io_requeset));

      	
		io_requeset.task_id = 0;
		memset(&dest_addr,0,sizeof(dest_addr));
		dest_addr.nl_family = AF_NETLINK;
		dest_addr.nl_pid = 0; //B：设置目的端口号
		dest_addr.nl_groups = 0;
		
		 scr_addr.nl_family = AF_NETLINK;
         scr_addr.nl_pid = getpid();
         scr_addr.nl_groups = 0;
 
         retval = bind(sock_fd, (struct sockaddr*)&scr_addr, sizeof(scr_addr));
         if(retval < 0)
         {
             printf("bind failed: %s", strerror(errno));
             close(sock_fd);
             return -1;
         }
	
	
		io_requeset.hdr.nlmsg_len = sizeof(struct nlmsghdr);
		io_requeset.hdr.nlmsg_pid = getpid(); //C：设置源端口
		io_requeset.hdr.nlmsg_flags = 0;
		io_requeset.hdr.nlmsg_type = 0x00000002;
//		strcpy(NLMSG_DATA(nlh),"Hello you!"); //设置消息体
		iov.iov_base = (void *)&io_requeset;
		iov.iov_len = sizeof(io_requeset);
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

		memset(&io_requeset,0,sizeof(io_requeset));
   		recv_smg = recvmsg(sock_fd,&msg,0);
   		printf("recv_smg\n");

		return 0;
}

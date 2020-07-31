#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <getopt.h>
#include <proto.h>
#include "client.h"
/*
*
* -M	--mgroup	zhi ding duo bo zu
* -P 	--port		zhi ding jie shou duan kuo
* -p	--player	zhi ding bo fang qi
* -H	--help
* */

static ssize_t writen(int fd , const char *buf , size_t len);
struct client_conf_st client_conf = {
	.rcvport = DEFAULT_RCVPORT,
	.mgroup = DEFAULT_MGROUP,
	.player_cmd = DEFAULT_PLAYERCMD
};


static void printhelp(){

	printf("-P --port	assign port\n\
		-M --mgroup	assign gourp\n\
		-p --player	assign player cmd\n\
		-H --help	display help\n");
}

int main(int argc , char** argv){
	int pd[2];
	pid_t pid;
	struct sockaddr_in laddr;
	int index = 0 , c;
	int sd;
	int chosenid;
	int val = 1;
	int len;
	struct ip_mreqn mreq;
	struct option argarr[5] = {{"port" , 1 , NULL , 'P'} ,
				 {"mgroup" , 1, NULL , 'M'} ,
				 {"player" , 1, NULL , 'p'},
				 {"help" , 0 , NULL , 'H'},
				 {NULL , 0 , NULL ,0}};
	while(1){
		c = getopt_long(argc , argv , "P:M:p:H", argarr , &index);
		if(c < 0)
			break;
		switch(c){
		case  'P':
			client_conf.rcvport = optarg;
			break;
		case  'M':
			client_conf.mgroup = optarg;
			break;
		case  'p':
			client_conf.player_cmd = optarg;
			break;
		case  'H':
			printhelp();
			exit(0);
			break;
		default:
			abort();
			break;
		}
	}
	sd = socket(AF_INET , SOCK_DGRAM , 0);
	if(sd < 0){
		perror("socket");
		exit(1);
	}

	inet_pton(AF_INET , client_conf.mgroup ,&mreq.imr_multiaddr);
	inet_pton(AF_INET , "0.0.0.0",&mreq.imr_address);
	mreq.imr_ifindex = if_nametoindex("eth0");

	if(setsockopt(sd , IPPROTO_IP , IP_ADD_MEMBERSHIP , &mreq , sizeof(mreq)) < 0){
		perror("setsockopt");
		exit(1);
	}
	if(setsockopt(sd , IPPROTO_IP , IP_MULTICAST_LOOP , &val , sizeof(val)) < 0){
		perror("setsockopt");
		exit(1);
	}
	laddr.sin_family = AF_INET;
	laddr.sin_port = htons(atoi(client_conf.rcvport));
	inet_pton(AF_INET ,"0.0.0.0" ,&laddr.sin_addr.s_addr);
	if(bind(sd , (void*)&laddr , sizeof(laddr)) < 0){
		perror("bind");
		exit(1);
	}
	
	if( pipe(pd) < 0){
		perror("pipe");
		exit(1);
	}

	pid = fork();
	if(pid < 0){
		perror("fork");
		exit(1);
	}
	if(pid == 0){
		close(sd);
		close(pd[1]);
		dup2(pd[0],0);
		if(pd[0] > 0)
			close(pd[0]);
		execl("/bin/sh" , "sh" , "-c" ,client_conf.player_cmd,NULL);
		perror("exec");
		exit(1);
	}


	struct msg_list_st *msg_list;
	struct sockaddr_in serveraddr;
	socklen_t serveraddr_len;
	msg_list = malloc(MSG_LIST_MAX);
	if(msg_list == NULL)
	{
		perror("malloc");
		exit(1);
	}
	serveraddr_len = sizeof(serveraddr);
	while(1){
		len = recvfrom(sd , msg_list , MSG_LIST_MAX , 0 , (void*)&serveraddr , &serveraddr_len);
		if(len < sizeof(struct msg_list_st))
		{
			fprintf(stderr , "message is too small.\n");
			continue;
		}
		if(msg_list->chnid != LISTCHNID)
		{
			
			fprintf(stderr , "chnid is not match ,recv chnid = %d", msg_list->chnid);
			continue;
		}
		break;
	}

	struct msg_listentry_st *pos;
	for(pos = msg_list->entry; (char*)pos < (((char*)msg_list) + len) ; pos = (void*)(((char*)pos) + ntohs(pos->len))){
		printf("channel %d : %s\n" , pos->chnid , pos->desc);
	}

	free(msg_list);
	puts("Please chosen channel:");
	int ret = -1;	
	while(ret < 1){
		ret = scanf("%d" , &chosenid);
		if( ret != 1)
			exit(1);
	}
	fprintf(stdout , "chosenid = %d\n" , chosenid);	
	struct msg_channel_st *msg_channel;
	struct sockaddr_in raddr;
	socklen_t raddr_len;
	msg_channel = malloc(MSG_CHANNEL_MAX);
	if(msg_channel == NULL)
	{
		perror("malloc");
		exit(1);
	}
	raddr_len = sizeof(raddr);
	while(1){
		len = recvfrom(sd , msg_channel , MSG_CHANNEL_MAX , 0 ,(void*)&raddr , &raddr_len);
		if(raddr.sin_addr.s_addr != serveraddr.sin_addr.s_addr || raddr.sin_port != serveraddr.sin_port)
		{
			fprintf(stderr , "Ignore :address not math.\n");
			continue;
		}

		if(len < sizeof(struct msg_channel_st)){
			fprintf(stderr , "Ignore :message too small\n");
			continue;
		}

		if(msg_channel->chnid == chosenid){
			fprintf(stdout , "\t accepted : msg:%d recieved \n", msg_channel->chnid);
			if( writen(pd[1] , msg_channel->data , len - sizeof(chnid_t)) < 0)
				exit(1);
		}
	
	}

	free(msg_channel);
	close(sd);
	exit(0);
}

static ssize_t writen(int fd , const char *buf , size_t len){
	
	int ret = 0 , pos = 0 ;
	while(len > 0){
		ret = write(fd , buf+pos , len);
		if(ret < 0){
			if (errno == EINTR)
				continue;
			perror("write");
			return -1;
		}
		len -= ret;
		pos += ret;
	}
	return pos;
}

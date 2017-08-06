/******************************************************************************************************************************************
* 文件名：fasthoptcp.c
* 文件描述：水库TCP模型-主控路由器代码。本模板代码作者王兆旭，为北京交通大学下一代互联网互联设备国家工程实验室新生基础工程技术培训专用。作者王兆旭在此郑重声明，此文件及其它用于培训的模板代码均为本人精力与经验的产物，本资源的传播方式均为作者本人向他人进行一对一传授，任何个人不得向第三方转交或展示该资源内容。任何有需求的学员，均须向王兆旭本人直接索要，亦无向他人索要或传授之权利和义务。因有些资源内容涉及实验室项目秘密，暂不考虑申请专利保护或软件著作权等事宜，故个别资源仅限实验室内部一对一发放，如发现有辜负作者本人的一片好意的行为，作者将保留就其原创性进行追查、举证、申诉和问责的权利。本资源的发放权归作者本人所有，其整理和总结过程浸透无偿贡献的热忱和为诸君学业尽绵薄之力的真诚，愿学员尊重作者的劳动成果，谢谢合作！
* 作者：王兆旭
* 身份：北京交通大学下一代互联网互联设备国家工程实验室 2013级硕博连读研究生
* E-mail. hellozxwang@foxmail.com
* Mobile. 18811774990
* QQ    . 535667240
* Addr  . 北京市海淀区西直门外北京交通大学机械楼D706室, 100044
*******************************************************************************************************************************************/
/******************************************************************************************************************************************
*****功能说明：水库TCP模型-主控路由器代码***************************************************************************************************
*******************************************************************************************************************************************/
/*
快速配置步骤：
1、宏定义修改
//仿真组网虚拟参量配置
#define PHYSICALPORT "eth0"	虚拟物理网口号——根据组装设备的需要修改为恰当的虚拟网口号，要求该网口所在的网络设备中，此网口号唯一
2、系统设置
在Fedora系统中因需要使用原始套接字发送自定义格式的数据包，须关闭Fedora的防火墙，命令：
sudo systemctl stop firewalld.service
在Ubuntu系统中无需任何操作
3、编译命令
gcc fasthoptcp.c -o fasthoptcp -lpthread -D_REENTRANT
4、运行（因涉及原始套接字的使用，须root权限）
sudo ./fasthoptcp
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <resolv.h>
#include <signal.h>
#include <semaphore.h>
#include <getopt.h>
#include <iconv.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*******************************************************************************************************************************************
*************************************宏定义配置数据************全局变量定义************包格式声明********************************************
*******************************************************************************************************************************************/
//MFTP窗口
#define MAXCWND 1000
long CWND;

//FLow Number
int FlowNumber;

//Point ROLE
enum
{
	ROLErouter,
	ROLEpublisher,
	ROLEsubscriber
}ROLE;

//eth num
int UpPortNum;
int DownPortNum;

//网卡端口（用于宏定义输入）
#define PHYSICALPORTlength 30
char PHYSICALPORTup[10][PHYSICALPORTlength];
char PHYSICALPORTdown[10][PHYSICALPORTlength];

//网卡端口（实际程序中使用并识别的载体）
unsigned char PhysicalPortup[10][PHYSICALPORTlength];
unsigned char PhysicalPortdown[10][PHYSICALPORTlength];

//Packet Loss Rate
int LOSSRATEup[10];//%%
int LOSSRATEdown[10];//%%

//缓冲区大小
#define SENDBUFSIZE 1024000
#define RECVBUFSIZE 1024000
#define SENDPKGSIZEup   1000
#define RECVPKGSIZEup   1000
#define SENDPKGSIZEdown 1000
#define RECVPKGSIZEdown 1000


//主控线程
void *thread_recvup(void *argv);
void *thread_recvdown(void *argv);
void *thread_senddown(void *argv);
void *thread_timer(void *argv);
//数据监视线程
void *thread_watcher(void *argv);

//预计RTO值
#define RTOwish 50000
#define PARA 2

//缓存队列长度
#define CacheQueueLen 10000
//缓存队列
struct CacheEvent
{
	int active;
	long GlobalID;
	long recv;
}Cache[CacheQueueLen];
//缓存队列头尾/长度
long CacheBottom;//占用队列无效位（已确认）
long CacheTop;//占用队列有效位（未确认）
long CacheCount;
long unfinishedCacheCount;
//缓存互斥锁
pthread_mutex_t CacheLock;
/*
	//锁定CacheLock
	pthread_mutex_lock(&CacheLock);
	//解锁CacheLock
	pthread_mutex_unlock(&CacheLock);
*/

//RECV队列长度
#define RECVQueueLen 10000
//RECV队列
struct RECVEvent
{
	int active;
	long chunksequence;
	long roundsequence;
	long size;
	long recv;
	long send;
	long cwnd;
	long timestamp;
	int ByteMap[MAXCWND];
	long GlobalID[MAXCWND];
}RECV[RECVQueueLen];
//RECV队列头尾/长度
long RECVBottom;//占用队列无效位（已确认）
long RECVTop;//占用队列有效位（未确认）
long RECVCountup;
long RECVCount;
//RECV互斥锁
pthread_mutex_t RECVLock;
/*
	//锁定RECVLock
	pthread_mutex_lock(&RECVLock);
	//解锁RECVLock
	pthread_mutex_unlock(&RECVLock);
*/

//SEND队列长度
#define SENDQueueLen 10000
//SEND队列
struct SENDEvent
{
	int active;
	long chunksequence;
	long roundsequence;
	long size;
	long recv;
	long send;
	long cwnd;
	long timestamp;
	int ByteMap[MAXCWND];
	long GlobalID[MAXCWND];
}SEND[SENDQueueLen];
//SEND队列头尾/长度
long SENDBottom;//占用队列无效位（已确认）
long SENDTop;//占用队列有效位（未确认）
long SENDCountup;
long SENDCount;
long SENDCountdown;
//SEND互斥锁
pthread_mutex_t SENDLock;
/*
	//锁定SENDLock
	pthread_mutex_lock(&SENDLock);
	//解锁SENDLock
	pthread_mutex_unlock(&SENDLock);
*/

//序列号
long RecvSequence;
long SendSequence;

//下行发包触发信号量
int FLAG_SENDEmpty;
sem_t SENDNotEmpty;
sem_t SendDownStart;

//程序开始运行的绝对时间
struct timeval INITstart;

//RTO维护参量
struct timeval RTTstop;
long RTT;
long RTO;

//辅助统计变量
long OUT_UpRecvCount[10];
long OUT_UpSendCount[10];
long OUT_DownSendCount;
long OUT_DownRecvCount;
long OUT_GoodputCount;

long OUT_ChunkUpRate;
long OUT_ChunkDownRate;

long OUT_TimeoutCount;

/*******************************************************************************************************************************************
*******************************************原始套接字接收数据包，解析从MAC层及以上的所有数据*************************************************
*******************************************************************************************************************************************/

/*****************************************
* 函数名称：Ethernet_SetPromisc
* 功能描述：物理网卡混杂模式属性操作
* 参数列表：
const char *PhysicalPortName
int SocketID
int iFlags
* 返回结果：
static int
*****************************************/
static int Ethernet_SetPromisc
(
	const char *PhysicalPortName,
	int SocketID,
	int Flags
)
{
	int ReturnValue;
	struct ifreq stIfr;
	
	//获取接口属性标志位
	strcpy(stIfr.ifr_name,PhysicalPortName);
	ReturnValue = ioctl(SocketID,SIOCGIFFLAGS,&stIfr);
	if(ReturnValue < 0)
	{
		perror("[Error]Get Interface Flags");   
		return -1;
	}
	
	if(Flags == 0)
	{
		//取消混杂模式
		stIfr.ifr_flags &= ~IFF_PROMISC;
	}
	else
	{
		//设置为混杂模式
		stIfr.ifr_flags |= IFF_PROMISC;
	}
	
	//设置接口标志
	ReturnValue = ioctl(SocketID,SIOCSIFFLAGS,&stIfr);
	if(ReturnValue < 0)
	{
		perror("[Error]Set Interface Flags");
		return -1;
	}
	
	return 0;
}

/*****************************************
* 函数名称：Ethernet_InitSocket
* 功能描述：创建原始套接字
* 参数列表：
* 返回结果：
static int
*****************************************/
static int Ethernet_InitSocket
(
	unsigned char * PhysicalPortin
)
{
	int ReturnValue;
	int SocketID;
	struct ifreq stIf;
	struct sockaddr_ll stLocal = {0};
	
	//创建SOCKET
	SocketID = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
	if(SocketID < 0)
	{
		perror("[Error]Initinate L2 raw socket");
		return -1;
	}
	
	//网卡混杂模式设置
	Ethernet_SetPromisc(PhysicalPortin,SocketID,1);
	
	//设置SOCKET选项
	int RecvBufSize = RECVBUFSIZE;
	ReturnValue = setsockopt(SocketID,SOL_SOCKET,SO_RCVBUF,&RecvBufSize,sizeof(int));
	if(ReturnValue < 0)
	{
		perror("[Error]Set socket option");
		close(SocketID);
		return -1;
	}
	
	//获取物理网卡接口索引
	strcpy(stIf.ifr_name,PhysicalPortin);
	ReturnValue = ioctl(SocketID,SIOCGIFINDEX,&stIf);
	if(ReturnValue < 0)
	{
		perror("[Error]Ioctl operation");
		close(SocketID);
		return -1;
	}
	
	//绑定物理网卡
	stLocal.sll_family = PF_PACKET;
	stLocal.sll_ifindex = stIf.ifr_ifindex;
	stLocal.sll_protocol = htons(ETH_P_ALL);
	ReturnValue = bind(SocketID,(struct sockaddr *)&stLocal,sizeof(stLocal));
	if(ReturnValue < 0)
	{
		perror("[Error]Bind the interface");
		close(SocketID);
		return -1;
	}

	int flags=fcntl(SocketID,F_GETFL,0);
	if (flags<0)
	{
		perror("[Error]Unable to Get socket flags.\n");
		close(SocketID);
		return -1;
	}
	ReturnValue = fcntl(SocketID,F_SETFL,flags &~ O_NONBLOCK);
	if(ReturnValue < 0)
	{
		perror("[Error]Unable to Set Socket flags.\n");
		close(SocketID);
		return -1;
	}
	
	return SocketID;   
}

/*****************************************
* 函数名称：main
* 功能描述：主函数，启动各个线程，自身不提供实际功能
* 参数列表：
* 返回结果：
*****************************************/
int main
(
	int argc,
	char *argv[]
)
{
	long i,j;
	//arg Input
	if(argc==1)
	{
		printf("No Chunk Size input\n");
		exit(0);
	}
	if(argc>=2)
	{
		FlowNumber=atoi(argv[1]);
		printf("Flow Number is %d\n",FlowNumber);
		SendSequence=FlowNumber*10000;
		printf("No Chunk Size input\n");
	}
	if(argc>=3)
	{
		CWND=atoi(argv[2]);
		printf("Chunk Size is %ld\n",CWND);
	}
	if(argc>=4)
	{
		if(memcmp(argv[3],"router",6)==0)
		{
			ROLE=ROLErouter;
		}
		if(memcmp(argv[3],"publisher",9)==0)
		{
			ROLE=ROLEpublisher;
		}
		if(memcmp(argv[3],"subscriber",10)==0)
		{
			ROLE=ROLEsubscriber;
		}
		printf("ROLE is %s\n",argv[3]);
	}
	if(argc>=5)
	{
		UpPortNum=atoi(argv[4]);
		printf("UpPortNum is %d\n",UpPortNum);
		for(i=0;i<UpPortNum;i++)
		{
			if(argv[5+i*2][0]=='e')
			{
				memcpy(PHYSICALPORTup[i]  ,argv[5+i*2],4);
				LOSSRATEup[i]=atoi(argv[5+i*2+1]);
			}
			else if(argv[5+i*2][0]=='h')
			{
				memcpy(PHYSICALPORTup[i]  ,argv[5+i*2],7);
				LOSSRATEup[i]=atoi(argv[5+i*2+1]);
			}
			printf("PHYSICALPORTup   %s\n",PHYSICALPORTup[i]);
			printf("LOSSRATEup       %d\n",LOSSRATEup[i]);
		}
	}
	if(argc>=6+UpPortNum*2)
	{
		DownPortNum=atoi(argv[5+UpPortNum*2]);
		printf("DownPortNum is %d\n",DownPortNum);
		for(i=0;i<DownPortNum;i++)
		{
			if(argv[6+UpPortNum*2+i*2][0]=='e')
			{
				memcpy(PHYSICALPORTdown[i],argv[6+UpPortNum*2+i*2],4);
				LOSSRATEdown[i]=atoi(argv[6+UpPortNum*2+i*2+1]);
			}
			else if(argv[6+UpPortNum*2+i*2][0]=='h')
			{
				memcpy(PHYSICALPORTdown[i],argv[6+UpPortNum*2+i*2],7);
				LOSSRATEdown[i]=atoi(argv[6+UpPortNum*2+i*2+1]);
			}
			printf("PHYSICALPORTdown %s\n",PHYSICALPORTdown[i]);
			printf("LOSSRATEdown     %d\n",LOSSRATEdown[i]);
		}
	}

	for(i=0;i<UpPortNum;i++)
	{
		memcpy(PhysicalPortup[i]  ,PHYSICALPORTup[i]  ,PHYSICALPORTlength);
	}
	for(i=0;i<DownPortNum;i++)
	{
		memcpy(PhysicalPortdown[i],PHYSICALPORTdown[i],PHYSICALPORTlength);
	}

	for(i=0;i<RECVQueueLen;i++)
	{
		RECV[i].active=-2;
		RECV[i].chunksequence=-1;
		RECV[i].roundsequence=-1;
		RECV[i].size=-1;
		RECV[i].recv=-1;
		RECV[i].send=-1;
		RECV[i].cwnd=-1;
		RECV[i].timestamp=0;
		for(j=0;j<MAXCWND;j++)
		{
			RECV[i].ByteMap[j]=0;
			RECV[i].GlobalID[j]=-1;
		}
	}
	for(i=0;i<SENDQueueLen;i++)
	{
		SEND[i].active=-2;
		SEND[i].chunksequence=-1;
		SEND[i].roundsequence=-1;
		SEND[i].size=-1;
		SEND[i].recv=-1;
		SEND[i].send=-1;
		SEND[i].cwnd=-1;
		SEND[i].timestamp=0;
		for(j=0;j<MAXCWND;j++)
		{
			SEND[i].ByteMap[j]=-1;
			SEND[i].GlobalID[j]=-1;
		}
	}
	for(i=0;i<CacheQueueLen;i++)
	{
		Cache[i].active=-1;
		Cache[i].GlobalID=-1;
		Cache[i].recv=0;
	}

	gettimeofday(&INITstart,NULL);
	
	srand(time(0));

	RTO=RTOwish;
/*
	CacheCount=0;
	pthread_mutex_init(&CacheLock,NULL);
*/
	RECVCount=0;
	pthread_mutex_init(&RECVLock,NULL);
	SENDCount=0;
	pthread_mutex_init(&SENDLock,NULL);

	FlowNumber=0;

	//创建数据监视线程
	pthread_t pthread_watcher;
	if(pthread_create(&pthread_watcher, NULL, thread_watcher, NULL)!=0)
	{
		perror("Creation of watcher thread failed.");
	}

	//Multiple Thread preCached Trans Parameter Num
	int threadPORTNUM[10];
	for(i=0;i<10;i++)
	{
		threadPORTNUM[i]=i;
	}

	if(ROLE!=ROLEpublisher)
	{
		//创建recvup线程
		pthread_t pthread_recvup[10];
		for(i=0;i<UpPortNum;i++)
		{
			if(pthread_create(&pthread_recvup[i], NULL, thread_recvup, (void *)&threadPORTNUM[i])!=0)
			{
				perror("Creation of recvup thread failed.");
			}
		}
	}


	if(ROLE!=ROLEsubscriber)
	{
		//创建recvdown线程
		pthread_t pthread_recvdown[10];
		for(i=0;i<DownPortNum;i++)
		{
			if(pthread_create(&pthread_recvdown[i], NULL, thread_recvdown, (void *)&threadPORTNUM[i])!=0)
			{
				perror("Creation of recvdown thread failed.");
			}
		}

		//创建senddown线程
		pthread_t pthread_senddown[10];
		for(i=0;i<DownPortNum;i++)
		{
			if(pthread_create(&pthread_senddown[i], NULL, thread_senddown, (void *)&threadPORTNUM[i])!=0)
			{
				perror("Creation of senddown thread failed.");
			}
		}
	}

	//创建timer线程
	pthread_t pthread_timer;
	if(pthread_create(&pthread_timer, NULL, thread_timer, NULL)!=0)
	{
		perror("Creation of timer thread failed.");
	}


	while(1)
	{
		sleep(1000);
	}
}



/*****************************************
* 函数名称：thread_recvup
* 功能描述：
* 参数列表：fd——连接套接字
* 返回结果：void
*****************************************/
void *thread_recvup
(
	void * argv
)
{
	int * pETHnum;
	pETHnum = (int *)argv;
	int ETHnum;
	ETHnum = *pETHnum;
	printf("[Thread_recvup]ETHnum  = %d\n",ETHnum);
	
	printf("[Thread_recvup]ETHport = %s\n",PhysicalPortup[ETHnum]);

//////////////////////////////////////////////////////////////////////////
	long i,j;
	
	int FLAG_RECVEventFound;
	long RecvChunkSequence;
	long RecvRoundSequence;
	long HopRecvCWNDcount;
	long AnnouncedSentCount;
	long RecvByteMap;
	long RecvGlobalID;
	struct timeval CURRENTtime;

	//SOCKET Init
	int RecvLength;
	int SendLength;
	socklen_t SocketLen = 0;
	int SocketID;
	SocketID = Ethernet_InitSocket(PhysicalPortup[ETHnum]);
	if(SocketID<0)
	{
		printf("error. RECVup SocketID init failed.\n");
		pthread_exit(NULL);
	}

	//RECV Buffer Set
	unsigned char RecvBuf[RECVBUFSIZE+20];
	//SEND Buffer Set
	unsigned char SendBuf[SENDPKGSIZEup+20];
	memset(SendBuf,0,SENDPKGSIZEup+20);
	SendBuf[0]=1;//上行包类型
	SendBuf[1]=0;
	while(1)
	{
		memset(RecvBuf,0,RECVBUFSIZE);
		RecvLength = recvfrom(SocketID, RecvBuf, RECVBUFSIZE, 0, NULL, &SocketLen);

		if(RecvLength<0)//Time Out
		{
			printf("RECVup failed, unusual.\n");
			continue;
		}

		//Random Loss
		if(rand()%10000<LOSSRATEup[ETHnum])
		{
			continue;
		}

		//筛包
		if(RecvBuf[0]!=0)//忽略非下行包
		{
			continue;
		}
		//only accept current flow number packet
		if(RecvBuf[1]!=0)//忽略
		{
			continue;
		}

		OUT_UpRecvCount[ETHnum]++;

		//提取序列号
		RecvChunkSequence=RecvBuf[3]*256*256*256+RecvBuf[4]*256*256+RecvBuf[5]*256+RecvBuf[6];
		RecvRoundSequence=RecvBuf[7]*256*256*256+RecvBuf[8]*256*256+RecvBuf[9]*256+RecvBuf[10];

		//锁定RECVLock
		pthread_mutex_lock(&RECVLock);

		i=RECVTop;
		while(1)
		{
			if(i==RECVBottom)
			{
				FLAG_RECVEventFound=0;
				break;
			}
			if(RECV[i].chunksequence==RecvChunkSequence)
			{
				if(RECV[i].active==-1)
				{
					if(RECV[i].roundsequence==RecvRoundSequence)
					{
						FLAG_RECVEventFound=2;
						break;
					}
					else
					{
						FLAG_RECVEventFound=1;
						break;
					}
				}
				else
				{
					FLAG_RECVEventFound=-1;
					break;
				}
			}
			i--;
			if(i==-1)
			{
				i=RECVQueueLen-1;
			}
		}
		//解锁RECVLock
		pthread_mutex_unlock(&RECVLock);

		if(FLAG_RECVEventFound==-1)
		{
//printf("RecvBuf[2]==%d\nRECV[i].active==%d\n",RecvBuf[2],RECV[i].active);
			//Send ACK
			SendBuf[2]=2;//本次窗口接收反馈包
			SendBuf[3]=RecvBuf[3];
			SendBuf[4]=RecvBuf[4];
			SendBuf[5]=RecvBuf[5];
			SendBuf[6]=RecvBuf[6];
			SendBuf[7]=RecvBuf[7];
			SendBuf[8]=RecvBuf[8];
			SendBuf[9]=RecvBuf[9];
			SendBuf[10]=RecvBuf[10];
			SendBuf[11]=RecvBuf[11];
			SendBuf[12]=RecvBuf[12];
			SendBuf[13]=RecvBuf[13];
			SendBuf[14]=RecvBuf[14];

			while(1)
			{
				SendLength = sendto(SocketID,&SendBuf,SENDPKGSIZEup,0,NULL,SocketLen);
				if(SendLength<0)
				{
					continue;
				}
				else
				{
					break;
				}
			}

			continue;
		}
		else if(FLAG_RECVEventFound==0)
		{
			//锁定RECVLock
			pthread_mutex_lock(&RECVLock);

			//添加计时表项
			RECVTop++;
			if(RECVTop>=RECVQueueLen)
				RECVTop=0;
			RECV[RECVTop].active=-1;
			RECV[RECVTop].chunksequence=RecvChunkSequence;
			RECV[RECVTop].roundsequence=0;
			RECV[RECVTop].size=0;
			RECV[RECVTop].recv=0;
			RECV[RECVTop].send=0;
			RECV[RECVTop].cwnd=0;
			RECV[RECVTop].timestamp=0;
			for(j=0;j<CWND;j++)
			{
				RECV[RECVTop].ByteMap[j]=0;
				RECV[RECVTop].GlobalID[j]=-1;
			}
			
			i=RECVTop;

			//解锁RECVLock
			pthread_mutex_unlock(&RECVLock);

			FLAG_RECVEventFound=1;

			RECVCountup++;
		}

		if(RecvBuf[2]==0)//数据包
		{
			if(FLAG_RECVEventFound==1)//There is possibility that all data packet lost, and two end packet carrying two continues sequence number received.
			{
				RECV[i].roundsequence=RecvRoundSequence;
				RECV[i].recv=0;
			}

			RECV[i].recv++;

			RecvByteMap=RecvBuf[11]*256*256*256+RecvBuf[12]*256*256+RecvBuf[13]*256+RecvBuf[14];
			RecvGlobalID=RecvBuf[15]*256*256*256+RecvBuf[16]*256*256+RecvBuf[17]*256+RecvBuf[18];

			RECV[i].ByteMap[RecvByteMap]=1;
			RECV[i].GlobalID[RecvByteMap]=RecvGlobalID;

			for(j=0;j<CacheQueueLen;j++)
			{
				if(Cache[j].active==1)
					continue;
				if(Cache[j].GlobalID==RecvGlobalID)
				{
					Cache[j].recv++;
					if(Cache[j].recv==1000)
					{
						Cache[j].active=1;
						unfinishedCacheCount--;
						CacheCount++;
					}
					break;
				}
				else if(Cache[j].active==-1)
				{
					Cache[j].active=0;
					Cache[j].GlobalID=RecvGlobalID;
					Cache[j].recv++;
					unfinishedCacheCount++;
					break;
				}
			}

			//COPY to SEND Queue
			//锁定SENDLock
			pthread_mutex_lock(&SENDLock);
			//Upload Finished Chunk
			if(SEND[SENDTop].active==-1 && SEND[SENDTop].recv==SEND[SENDTop].size)
			{
				SEND[SENDTop].active=0;
				SEND[SENDTop].roundsequence=0;
				SEND[SENDTop].recv=0;
				SEND[SENDTop].send=0;
				SEND[SENDTop].timestamp=0;

				SENDCount++;
				SENDCountup--;
				
				//printf("New Chunk Received.\n");
			}
			//添加SEND表项
			if(SEND[SENDTop].active!=-1)
			{
				SENDTop++;
				if(SENDTop==SENDQueueLen)
					SENDTop=0;

				SEND[SENDTop].active=-1;
				SEND[SENDTop].chunksequence=SendSequence;
				SEND[SENDTop].roundsequence=0;
				SEND[SENDTop].size=CWND;
				SEND[SENDTop].recv=0;
				SEND[SENDTop].send=0;
				SEND[SENDTop].cwnd=0;
				SEND[SENDTop].timestamp=0;
				for(j=0;j<SEND[SENDTop].size;j++)
				{
					SEND[SENDTop].ByteMap[j]=0;
					SEND[SENDTop].GlobalID[j]=-1;
				}
				SendSequence++;
				
				SENDCountup++;
			}

			SEND[SENDTop].GlobalID[SEND[SENDTop].recv]=RecvGlobalID;
			SEND[SENDTop].recv++;

			//解锁SENDLock
			pthread_mutex_unlock(&SENDLock);
		}
		else if(RecvBuf[2]==1)//本次窗口发送终止包
		{
if(0)
{
printf("[recvup]New Round Recv!\n");
printf("[%ld]active   = %d\n",i,RECV[i].active);
printf("[%ld]chunk    = %ld\n",i,RECV[i].chunksequence);
printf("[%ld]round    = %ld\n",i,RECV[i].roundsequence);
printf("[%ld]size     = %ld\n",i,RECV[i].size);
printf("[%ld]recv     = %ld\n",i,RECV[i].recv);
printf("[%ld]send     = %ld\n",i,RECV[i].send);
printf("[%ld]cwnd     = %ld\n",i,RECV[i].cwnd);
printf("RECVTop       = %ld\n",RECVTop);
printf("RECVBottom    = %ld\n",RECVBottom);
printf("ByteMap:\n");
for(j=0;j<1000;j++)
{
	printf("%d",RECV[i].ByteMap[j]);
}
printf("\n[=============================\n");
}
			//Send ACK
			SendBuf[2]=2;//本次窗口接收反馈包
			SendBuf[3]=RecvBuf[3];
			SendBuf[4]=RecvBuf[4];
			SendBuf[5]=RecvBuf[5];
			SendBuf[6]=RecvBuf[6];
			SendBuf[7]=RecvBuf[7];
			SendBuf[8]=RecvBuf[8];
			SendBuf[9]=RecvBuf[9];
			SendBuf[10]=RecvBuf[10];
			SendBuf[11]=RECV[i].recv/256/256/256;
			SendBuf[12]=RECV[i].recv/256/256%256;
			SendBuf[13]=RECV[i].recv/256%256;
			SendBuf[14]=RECV[i].recv%256;

			for(j=0;j<SENDPKGSIZEup;j++)
			{
				SendBuf[j+20]=(unsigned char)RECV[i].ByteMap[j];
			}

			while(1)
			{
				SendLength = sendto(SocketID,&SendBuf,SENDPKGSIZEup+20,0,NULL,SocketLen);
				if(SendLength<0)
				{
					continue;
				}
				else
				{
					break;
				}
			}

			AnnouncedSentCount=RecvBuf[11]*256*256*256+RecvBuf[12]*256*256+RecvBuf[13]*256+RecvBuf[14];

			OUT_UpSendCount[ETHnum]++;

			if(RECV[i].size==0)
			{
				RECV[i].size=AnnouncedSentCount;
			}
		
			if(AnnouncedSentCount!=RECV[i].recv)
			{
				//RECV[i].recv=0;
			}
			
			if(AnnouncedSentCount==RECV[i].recv)
			{
				//锁定RECVLock
				pthread_mutex_lock(&RECVLock);
				
				RECV[i].active=0;
				RECV[i].roundsequence=0;
				RECV[i].recv=0;
				RECV[i].send=0;
				RECV[i].timestamp=0;

				//解锁RECVLock
				pthread_mutex_unlock(&RECVLock);

				RECVCountup--;
				RECVCount++;
				OUT_ChunkUpRate++;
			}
		}
		else if(RecvBuf[2]==3)//整个任务终止包
		{
			//UDP面向外界触发
			printf("Trans Stop.\n");
		}
	}

	close(SocketID);
}

/*****************************************
* 函数名称：thread_recvdown
* 功能描述：
* 参数列表：fd——连接套接字
* 返回结果：void
*****************************************/
void *thread_recvdown
(
	void * argv
)
{
	int * pETHnum;
	pETHnum = (int *)argv;
	int ETHnum;
	ETHnum = *pETHnum;
	printf("[Thread_recvdown]ETHnum  = %d\n",ETHnum);
	
	printf("[Thread_recvdown]ETHport = %s\n",PhysicalPortdown[ETHnum]);

//////////////////////////////////////////////////////////////////////////
	long i,j;
	
	int FLAG_SENDEventFound;
	long RecvChunkSequence;
	long RecvRoundSequence;
	long AnnouncedRecvCount;

	//SOCKET Init
	int RecvLength;
	socklen_t SocketLen = 0;
	int SocketID;
	SocketID = Ethernet_InitSocket(PhysicalPortdown[ETHnum]);
	if(SocketID<0)
	{
		printf("error. RECVdown SocketID init failed.\n");
		pthread_exit(NULL);
	}

	//RECV Buffer Set
	unsigned char RecvBuf[RECVBUFSIZE];

	while(1)
	{
		memset(RecvBuf,0,RECVBUFSIZE);
		RecvLength = recvfrom(SocketID, RecvBuf, RECVBUFSIZE, 0, NULL, &SocketLen);

		if(RecvLength<0)
		{
			printf("RECVdown failed, unusual.\n");
			continue;
		}

		//Random Loss
		if(rand()%10000<LOSSRATEdown[ETHnum])
		{
			continue;
		}

		//筛包
		if(RecvBuf[0]!=1 || RecvBuf[2]!=2)//忽略非上行包，忽略非窗口反馈包
		{
			continue;
		}
		//only accept current flow number packet
		if(RecvBuf[1]!=0)//忽略
		{
			continue;
		}

		OUT_DownRecvCount++;

		//提取序列号
		RecvChunkSequence=RecvBuf[3]*256*256*256+RecvBuf[4]*256*256+RecvBuf[5]*256+RecvBuf[6];
		RecvRoundSequence=RecvBuf[7]*256*256*256+RecvBuf[8]*256*256+RecvBuf[9]*256+RecvBuf[10];
		AnnouncedRecvCount=RecvBuf[11]*256*256*256+RecvBuf[12]*256*256+RecvBuf[13]*256+RecvBuf[14];

		//锁定SENDLock
		pthread_mutex_lock(&SENDLock);

		i=SENDBottom;
		while(1)
		{
			if(i==SENDTop+1)
			{
				FLAG_SENDEventFound=0;
				break;
			}

			if(SEND[i].active==0)
			{
				if(SEND[i].chunksequence==RecvChunkSequence)
				{
					if(SEND[i].roundsequence==RecvRoundSequence)
					{
						FLAG_SENDEventFound=2;
						break;
					}
					else
					{
						FLAG_SENDEventFound=1;
						break;
					}
				}
			}
			i++;
			if(i==SENDQueueLen)
			{
				i=0;
			}
		}
		//解锁SENDLock
		pthread_mutex_unlock(&SENDLock);

		if(FLAG_SENDEventFound<=1)
		{
			continue;
		}
		else if(FLAG_SENDEventFound==2)
		{
			//记录RTT
			gettimeofday(&RTTstop,NULL);
			RTT=(RTTstop.tv_sec-INITstart.tv_sec)*1000000+RTTstop.tv_usec-INITstart.tv_usec;
			RTT-=SEND[i].timestamp;
			//维护RTO
			RTO=(float)RTO*(float)0.99+(float)RTT*(float)0.01*PARA;

			SEND[i].timestamp=0;

			//将收方反馈信息处理并呈递给下行发包
			AnnouncedRecvCount=RecvBuf[11]*256*256*256+RecvBuf[12]*256*256+RecvBuf[13]*256+RecvBuf[14];

//if(ROLE==ROLErouter)

			if(AnnouncedRecvCount>=SEND[i].cwnd)//all data packets trans success
			{
				//delete计时表项
				SEND[i].active=-2;
				SEND[i].chunksequence=-1;
				SEND[i].roundsequence=-1;
				SEND[i].size=-1;
				SEND[i].recv=-1;
				SEND[i].send=-1;
				SEND[i].cwnd=-1;
				SEND[i].timestamp=0;
				for(j=0;j<SEND[i].size;j++)
				{
					SEND[i].ByteMap[j]=-1;
					SEND[i].GlobalID[j]=-1;
				}

				SENDCountdown--;
				OUT_ChunkDownRate++;

if(0)
{
	printf("[recvdown]New ACK Packet recv/\n");
	printf("[%ld]active        = %d\n",i,SEND[i].active);
	printf("[%ld]chunk.local   = %ld\n",i,SEND[i].chunksequence);
	printf("[%ld]Chunk.recv    = %ld\n",i,RecvChunkSequence);
	printf("[%ld]round.local   = %ld\n",i,SEND[i].roundsequence);
	printf("[%ld]Round.recv    = %ld\n",i,RecvRoundSequence);
	printf("[%ld]size          = %ld\n",i,SEND[i].size);
	printf("[%ld]recv          = %ld\n",i,SEND[i].recv);
	printf("[%ld]send          = %ld\n",i,SEND[i].send);
	printf("[%ld]cwnd.local    = %ld\n",i,SEND[i].cwnd);
	printf("[%ld]CWND.recv     = %ld\n",i,AnnouncedRecvCount);
	printf("SENDTop           = %ld\n",SENDTop);
	printf("SENDBottom        = %ld\n",SENDBottom);
	printf("[=============================\n");
}
			}
			else if(AnnouncedRecvCount<SEND[i].cwnd)//some data packets lost
			{
				//change计时表项
				SEND[i].active=1;
				SEND[i].roundsequence++;
				SEND[i].send=SEND[i].size-SEND[i].cwnd+AnnouncedRecvCount;
				SEND[i].cwnd-=AnnouncedRecvCount;
				for(j=0;j<SEND[i].size;j++)
				{
					if(RecvBuf[j+20]==1)
					{
						SEND[i].ByteMap[j]=2;
					}
					else if(RecvBuf[j+20]==0)
					{
						SEND[i].ByteMap[j]=0;
					}
					else
					{
						SEND[i].ByteMap[j]=3;
					}
				}
if(0)
{
	printf("[recvdown]New ACK Packet recv/\n");
	printf("[%ld]active        = %d\n",i,SEND[i].active);
	printf("[%ld]chunk.local   = %ld\n",i,SEND[i].chunksequence);
	printf("[%ld]Chunk.recv    = %ld\n",i,RecvChunkSequence);
	printf("[%ld]round.local   = %ld\n",i,SEND[i].roundsequence);
	printf("[%ld]Round.recv    = %ld\n",i,RecvRoundSequence);
	printf("[%ld]size          = %ld\n",i,SEND[i].size);
	printf("[%ld]recv          = %ld\n",i,SEND[i].recv);
	printf("[%ld]send          = %ld\n",i,SEND[i].send);
	printf("[%ld]cwnd.local    = %ld\n",i,SEND[i].cwnd);
	printf("[%ld]CWND.recv     = %ld\n",i,AnnouncedRecvCount);
	printf("SENDTop           = %ld\n",SENDTop);
	printf("SENDBottom        = %ld\n",SENDBottom);
	printf("[=============================\n");
}
			}
			else
			{
				usleep(100);
				printf("AnnouncedRecvCount==%ld\n",AnnouncedRecvCount);
				printf("SEND[i].cwnd     ==%ld\n",SEND[i].cwnd);
				printf("AnnouncedRecvCount>CWND, Not normal.\n");

				exit(0);
			}
		}
	}

	close(SocketID);
}

/*****************************************
* 函数名称：thread_senddown
* 功能描述：
* 参数列表：fd——连接套接字
* 返回结果：void
*****************************************/
void *thread_senddown
(
	void * argv
)
{
	int * pETHnum;
	pETHnum = (int *)argv;
	int ETHnum;
	ETHnum = *pETHnum;
	printf("[Thread_senddown]ETHnum  = %d\n",ETHnum);
	
	printf("[Thread_senddown]ETHport = %s\n",PhysicalPortdown[ETHnum]);

//////////////////////////////////////////////////////////////////////////
	long i,j;

	int FLAG_SENDEventFound;
	sem_wait(&SendDownStart);
	int senddata;
	struct timeval CURRENTtime;

	//SOCKET Init
	int SendLength;
	socklen_t SocketLen = 0;
	int SocketID;
	SocketID = Ethernet_InitSocket(PhysicalPortdown[ETHnum]);
	if(SocketID<0)
	{
		printf("error. SENDdown SocketID init failed.\n");
		pthread_exit(NULL);
	}

	//SEND Buffer Set
	unsigned char SendBuf[SENDPKGSIZEdown+20];
	memset(SendBuf,0,SENDPKGSIZEdown+20);
	SendBuf[0]=0;//下行包类型
	SendBuf[1]=0;
	
	while(1)
	{
		//锁定SENDLock
		pthread_mutex_lock(&SENDLock);

		i=SENDBottom;
		while(1)
		{
			if(i==SENDTop+1)
			{
				FLAG_SENDEventFound=0;
				break;
			}
			if(SEND[i].active==1)
			{
				FLAG_SENDEventFound=1;
				break;
			}
			i++;
			if(i==SENDQueueLen)
			{
				i=0;
			}
		}
		if(FLAG_SENDEventFound==0)
		{
			i=SENDBottom;
			while(1)
			{
				if(i==SENDTop+1)
				{
					FLAG_SENDEventFound=0;
					break;
				}
				if(SEND[i].active==0 && SEND[i].send==0)
				{
					SEND[i].active==1;
					SEND[i].roundsequence++;
					SEND[i].cwnd=SEND[i].size-SEND[i].send;
					FLAG_SENDEventFound=1;

					SENDCount--;
					SENDCountdown++;

					break;
				}
				i++;
				if(i==SENDQueueLen)
				{
					i=0;
				}
			}
		}

		if(FLAG_SENDEventFound==0 && ROLE==ROLEpublisher)
		{
			//添加计时表项
			SENDTop++;

			if(SENDTop>=SENDQueueLen)
				SENDTop=0;
			SEND[SENDTop].active=1;
			SEND[SENDTop].chunksequence=SendSequence;
			SEND[SENDTop].roundsequence=0;
			SEND[SENDTop].size=CWND;
			SEND[SENDTop].recv=0;
			SEND[SENDTop].send=0;
			SEND[SENDTop].cwnd=CWND;
			SEND[SENDTop].timestamp=0;
			for(j=0;j<SEND[SENDTop].size;j++)
			{
				SEND[SENDTop].ByteMap[j]=0;
				SEND[SENDTop].GlobalID[j]=SendSequence;
			}
			SendSequence++;
			SENDCountdown++;

			i=SENDTop;

			FLAG_SENDEventFound=1;
		}

		//解锁SENDLock
		pthread_mutex_unlock(&SENDLock);

if(0)
{
printf("[senddown]New Round Sent!\n");
printf("[%ld]active   = %d\n",i,SEND[i].active);
printf("[%ld]chunk    = %ld\n",i,SEND[i].chunksequence);
printf("[%ld]round    = %ld\n",i,SEND[i].roundsequence);
printf("[%ld]size     = %ld\n",i,SEND[i].size);
printf("[%ld]recv     = %ld\n",i,SEND[i].recv);
printf("[%ld]send     = %ld\n",i,SEND[i].send);
printf("[%ld]cwnd     = %ld\n",i,SEND[i].cwnd);
printf("SENDTop       = %ld\n",SENDTop);
printf("SENDBottom    = %ld\n",SENDBottom);
printf("ByteMap:\n");
for(j=0;j<SEND[i].size;j++)
{
	printf("%d",SEND[i].ByteMap[j]);
}
printf("\n[=============================\n");
}

		if(FLAG_SENDEventFound==0)
		{
			//printf("No SEND to send.\n");
			FLAG_SENDEmpty=1;
			sem_wait(&SENDNotEmpty);
			//printf("SENDNotEmpty trigered.\n");
		}
		else if(FLAG_SENDEventFound==1)
		{
			SendBuf[2]=0;//数据包
			SendBuf[3]=SEND[i].chunksequence/256/256/256;
			SendBuf[4]=SEND[i].chunksequence/256/256%256;
			SendBuf[5]=SEND[i].chunksequence/256%256;
			SendBuf[6]=SEND[i].chunksequence%256;

			SendBuf[7]=SEND[i].roundsequence/256/256/256;
			SendBuf[8]=SEND[i].roundsequence/256/256%256;
			SendBuf[9]=SEND[i].roundsequence/256%256;
			SendBuf[10]=SEND[i].roundsequence%256;

			while(1)
			{
				//usleep(100);

				if(SEND[i].send==SEND[i].size)
				{
					break;
				}

				for(j=0;j<SEND[i].size;j++)
				{
					if(SEND[i].ByteMap[j]==0)
					{
						SendBuf[11]=j/256/256/256;
						SendBuf[12]=j/256/256%256;
						SendBuf[13]=j/256%256;
						SendBuf[14]=j%256;
						SendBuf[15]=SEND[i].GlobalID[j]/256/256/256;
						SendBuf[16]=SEND[i].GlobalID[j]/256/256%256;
						SendBuf[17]=SEND[i].GlobalID[j]/256%256;
						SendBuf[18]=SEND[i].GlobalID[j]%256;
						
						break;
					}
				}
				if(j==SEND[i].size)
				{
					printf("SEND[i].send=%ld\n",SEND[i].send);
					printf("Window Count error.\n");
					exit(0);
				}

				while(1)
				{
					SendLength = sendto(SocketID,&SendBuf,SENDPKGSIZEdown,0,NULL,SocketLen);
					if(SendLength<0)
					{
						//printf("0");
						continue;
					}
					else
					{
						break;
					}
				}
				SEND[i].ByteMap[j]=1;
				SEND[i].send++;

				OUT_DownSendCount++;
			}

if(0)
{
for(j=0;j<SEND[i].size;j++)
{
	printf("%d",SEND[i].ByteMap[j]);
}
printf("\n[=============================\n");
}

			SEND[i].active=0;

			gettimeofday(&CURRENTtime,NULL);
			SEND[i].timestamp=(CURRENTtime.tv_sec-INITstart.tv_sec)*1000000+CURRENTtime.tv_usec-INITstart.tv_usec;

			SendBuf[2]=1;//本次窗口发送终止包
			SendBuf[11]=SEND[i].cwnd/256/256/256;
			SendBuf[12]=SEND[i].cwnd/256/256%256;
			SendBuf[13]=SEND[i].cwnd/256%256;
			SendBuf[14]=SEND[i].cwnd%256;
			memset(SendBuf+20,0,SENDPKGSIZEdown);
			for(j=0;j<SEND[i].size;j++)
			{
				SendBuf[j+20] = (unsigned char)SEND[i].ByteMap[j];
			}

			while(1)
			{
				SendLength = sendto(SocketID,&SendBuf,SENDPKGSIZEdown+20,0,NULL,SocketLen);
				if(SendLength<0)
				{
					//printf("0");
					continue;
				}
				else
				{
					break;
				}
			}
			
			//printf("[%ld]SEND[i].chunksequence\n",SEND[i].chunksequence);
			
			OUT_DownSendCount++;
		}
	}
	
	close(SocketID);
}

/*****************************************
* 函数名称：thread_timer
* 功能描述：
* 参数列表：fd——连接套接字
* 返回结果：void
*****************************************/
void *thread_timer
(
	void * argv
)
{
	struct timeval CURRENTtime;
	long timenow,timeout;

	long TablePoint;
	long SENDLength;
	TablePoint=0;
	SENDLength=0;

	SENDBottom=0;
	SENDTop=0;
	
	CacheCount=0;
	unfinishedCacheCount=0;

	FLAG_SENDEmpty=0;
	sem_post(&SendDownStart);
	while(1)
	{
		usleep(100);

		//采集当前时间，并计算距离程序开始已经消耗的时间
		gettimeofday(&CURRENTtime,NULL);
		timenow=(CURRENTtime.tv_sec-INITstart.tv_sec)*1000000+CURRENTtime.tv_usec-INITstart.tv_usec;
		//设置超时
		//timeout=timenow-RTO;
		timeout=timenow-RTOwish;

		TablePoint=SENDBottom;
		while(1)
		{
			if(TablePoint==SENDTop+1)
			{
				break;
			}
			
			if(FLAG_SENDEmpty==1 && (SEND[TablePoint].active==1 || (SEND[TablePoint].active==0 && SEND[TablePoint].send==0)))
			{
				//printf("TablePoint=%ld\n",TablePoint);
				FLAG_SENDEmpty=0;
				sem_post(&SENDNotEmpty);
			}

			//遇到未经ACK确认的超时表项
			if(SEND[TablePoint].active==0 
			&& SEND[TablePoint].send==SEND[TablePoint].size 
			&& SEND[TablePoint].timestamp>0 
			&& SEND[TablePoint].timestamp<=timeout)
			{
				//change此表项
				SEND[TablePoint].active=1;
				SEND[TablePoint].timestamp=0;

				OUT_TimeoutCount++;
			}
			
			TablePoint++;
			if(TablePoint==SENDQueueLen)
				TablePoint=0;
		}

		//SENDBottom跟进
		SENDLength=SENDTop-SENDBottom;
		if(SENDLength<0)
		{
			SENDLength=SENDQueueLen+SENDLength;
		}
		while(1)
		{
			if(SENDBottom!=SENDQueueLen-1 && SEND[SENDBottom+1].active==-1 && SENDLength>SENDQueueLen*0.1)//结束判断1
			{
				//delete计时表项
				SEND[SENDBottom+1].active=-2;
				SEND[SENDBottom+1].chunksequence=-1;
				SEND[SENDBottom+1].roundsequence=-1;
				SEND[SENDBottom+1].size=-1;
				SEND[SENDBottom+1].recv=-1;
				SEND[SENDBottom+1].send=-1;
				SEND[SENDBottom+1].cwnd=-1;
				SEND[SENDBottom+1].timestamp=0;
			}
			else if(SENDBottom==SENDQueueLen-1 && SEND[0].active==-1 && SENDLength>SENDQueueLen*0.1)
			{
				//delete计时表项
				SEND[0].active=-2;
				SEND[0].chunksequence=-1;
				SEND[0].roundsequence=-1;
				SEND[0].size=-1;
				SEND[0].recv=-1;
				SEND[0].send=-1;
				SEND[0].cwnd=-1;
				SEND[0].timestamp=0;
			}

			if((SENDBottom!=SENDQueueLen-1 && SEND[SENDBottom+1].active!=-2)
			|| (SENDBottom==SENDQueueLen-1 && SEND[0].active!=-2))//结束判断1
			{
				break;
			}

			if(SENDBottom==SENDTop)//结束判断2
				break;
			
			SENDBottom++;
			if(SENDBottom==SENDQueueLen)//计数器SENDBottom队列回环
				SENDBottom=0;
			SENDLength--;
		}
	}
}

/*****************************************
* 函数名称：thread_watcher
* 功能描述：
* 参数列表：fd——连接套接字
* 返回结果：void
*****************************************/
void *thread_watcher
(
	void * argv
)
{
	int i;

	int outputactivetable;
	outputactivetable=1;

	long TablePoint;
	long CacheCost;
	time_t GMT;//国际标准时间，实例化time_t结构(typedef long time_t;)
	//创建socket
	//套接口描述字
	int AnnounceUDP;
	AnnounceUDP = socket(AF_INET,SOCK_DGRAM,0);
	struct sockaddr_in addrsend;
	bzero(&addrsend,sizeof(addrsend));
	addrsend.sin_family=AF_INET;
	addrsend.sin_port=htons(6000);
	addrsend.sin_addr.s_addr=inet_addr("127.0.0.1");//htonl将主机字节序转换为网络字节序
	unsigned char UDPBUF[50];

	long CWNDleft;
	
	struct timeval CURRENTtime;
	long timenow,timeout;

	long KBpsUpRecvRate;
	long KBpsUpSendRate;
	long KBpsDownRecvRate;
	long KBpsDownSendRate;

	long KBpsGoodRate;

	while(1)
	{
		for(i=0;i<UpPortNum;i++)
		{
			OUT_UpRecvCount[i]=0;
		}
		OUT_DownSendCount=0;
		OUT_DownRecvCount=0;

		OUT_GoodputCount=0;

		OUT_ChunkUpRate=0;
		OUT_ChunkDownRate=0;
		
		OUT_TimeoutCount=0;

		//usleep(100000);
		sleep(1);

		//记录程序运行时间
		gettimeofday(&CURRENTtime,NULL);
		timenow=(CURRENTtime.tv_sec-INITstart.tv_sec)*1000000+CURRENTtime.tv_usec-INITstart.tv_usec;

		//计算数据速率
		KBpsUpRecvRate=0;
		KBpsUpSendRate=0;
		KBpsDownRecvRate=0;
		KBpsDownSendRate=0;
		
		KBpsGoodRate=0;

		for(i=0;i<UpPortNum;i++)
		{
			KBpsUpRecvRate += OUT_UpRecvCount[i];
			KBpsUpSendRate += OUT_UpSendCount[i];
		}
		KBpsDownSendRate = OUT_DownSendCount;
		KBpsDownRecvRate = OUT_DownRecvCount;

		KBpsGoodRate = OUT_GoodputCount;
/*
		if(KBpsUpRecvRate==0 && CacheCount!=0 && outputactivetable==1 && ROLE!=ROLEpublisher)
		{
			TablePoint=CacheBottom;
			while(1)
			{
				if(TablePoint==CacheTop+1)
				{
					break;
				}

				TablePoint++;
				if(TablePoint==CacheQueueLen)
					TablePoint=0;

				printf("[Chunk %ld]active=%d  chunksequence=%ld\n",TablePoint,Cache[TablePoint].active,Cache[TablePoint].chunksequence);
			}
			outputactivetable=0;
		}
		else if(KBpsUpRecvRate>0)
		{
			if(outputactivetable==0)
				outputactivetable=1;
		}
*/

if(1)
{
	printf("Up   Recv Rate  = %ld Mbps\n",KBpsUpRecvRate*8/1000);
	printf("Up   Send Rate  = %ld Mbps\n",KBpsUpSendRate*8/1000);
	printf("Down Recv Rate  = %ld Mbps\n",KBpsDownRecvRate*8/1000);
	printf("Down Send Rate  = %ld Mbps\n",KBpsDownSendRate*8/1000);
	printf("Up   Batch Rate = %ld Batches/s\n",OUT_ChunkUpRate);
	printf("Down Batch Rate = %ld Batches/s\n",OUT_ChunkDownRate);
	printf("------------------------------------------\n");
	printf("finishedChunk   = %ld Chunks\n",CacheCount);
	printf("unfinishedChunk = %ld Chunks\n",unfinishedCacheCount);
	printf("------------------------------------------\n");
	printf("RECVCount.up    = %ld Batches\n",RECVCountup);
	printf("RECVCount       = %ld Batches\n",RECVCount);
	printf("RECVTop         = %ld\n",RECVTop);
	printf("RECVBottom      = %ld\n",RECVBottom);
	printf("------------------------------------------\n");
	printf("SENDCount.up    = %ld Batches\n",SENDCountup);
	printf("SENDCount.      = %ld Batches\n",SENDCount);
	printf("SENDCount.down  = %ld Batches\n",SENDCountdown);
	printf("SENDTop         = %ld\n",SENDTop);
	printf("SENDBottom      = %ld\n",SENDBottom);
	printf("------------------------------------------\n");
	printf("Timeout         = %ld Times\n",OUT_TimeoutCount);
	printf("==========================================\n");
}

		if(KBpsUpRecvRate==0)
		{
			//printf("0");
		}
		else
		{
/*
			if(ROLE==ROLErouter)
			{
				time(&GMT);//读取GMT，赋值给GMT
				UDPBUF[0]=GMT/256/256/256;
				UDPBUF[1]=GMT/256/256%256;
				UDPBUF[2]=GMT/256%256;
				UDPBUF[3]=GMT%256;
				//CacheCost=CacheCount*MAXCWND+CWND;
				for(i=0;i<UpPortNum;i++)
				{
					CacheCost += OUT_CWNDrecvadd[i];
				}
				UDPBUF[4]=CacheCost/256/256/256;
				UDPBUF[5]=CacheCost/256/256%256;
				UDPBUF[6]=CacheCost/256%256;
				UDPBUF[7]=CacheCost%256;
				UDPBUF[8]=KBpsDownRate/256/256/256;
				UDPBUF[9]=KBpsDownRate/256/256%256;
				UDPBUF[10]=KBpsDownRate/256%256;
				UDPBUF[11]=KBpsDownRate%256;
				UDPBUF[12]=KBpsGoodRate/256/256/256;
				UDPBUF[13]=KBpsGoodRate/256/256%256;
				UDPBUF[14]=KBpsGoodRate/256%256;
				UDPBUF[15]=KBpsGoodRate%256;
				//发送南向触发指令
				sendto(AnnounceUDP,UDPBUF,50,0,(struct sockaddr *)&addrsend,sizeof(addrsend));
			}
*/
		}
	}
	close(AnnounceUDP);
}

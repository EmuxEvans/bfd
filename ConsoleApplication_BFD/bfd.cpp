#pragma comment(lib, "winmm.lib")
#include "winsock2.h"
#include "ws2tcpip.h"
#include "windows.h"
#include "stdio.h"
#include "stdint.h"
#include "conio.h"
#include "time.h"

#include "bfd.h"
#include "common.h"



enum BFD_MESSAGE_TYPE
{
	MESSAGE_TYPE_TX_TIMEOUT = WM_USER + 1,
	MESSAGE_TYPE_RX_TIMEOUT,
	MESSAGE_TYPE_SESSION_CANCEL,
	MESSAGE_TYPE_PARAMETER_UPDATA,
	MESSAGE_TYPE_DATA_RECIVE,
	MESSAGE_TYPE_ADMIN_DOWN,
};

#define MAX_DIAGNOSTIC_NUMBER			(8)

#define BFD_IP_PACKAGE_SIZE				(52)	/*��Raw Socket����,IP��Ϊ52���ֽ�,��֧����֤,���������,����*/
#define BFD_SEND_PORT_INIT				(49152)
#define BFD_INIT_TIME					(1000000)/*1000ms*/

#define BFD_STR_UP		"\n*%s %d %d:%d:%d: BFD session [destination address:%s, source address: %s, local-discriminator:%d] UP"
#define BFD_STR_DOWN	"\n*%s %d %d:%d:%d: BFD session [destination address:%s, source address: %s, local-discriminator:%d] DOWN, Reason:%s"


static SessionNode_t *sessionListHead = NULL;
static UINT16 currentPort = BFD_SEND_PORT_INIT;/*��49152��ʼ��������,ÿ���Ựһ���˿�*/
static UINT32 localDiscreaminator = 1;
static HANDLE hSessionMutex;/*����������ǰ��port��discreaminator*/
static BOOL BFDhasInitialized = FALSE;

/*��������ַ�����С��������*/
static char bfdStateChangeString[255];
static char *bfdDiagnosticReason[MAX_DIAGNOSTIC_NUMBER]{
	"No Diagnostic",
	"Control Detection Time Expired",
	"Echo Function Failed",
	"Neighbor Signaled Session Down",
	"Forwarding Plane Reset",
	"Path Down",
	"Concatenated Path Down",
	"Administratively Down",
};
static char *month[12] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec",
};



static inline void getCurrentTime(struct tm *outtime)
{
	time_t currentTime;
	time(&currentTime);
	localtime_s(outtime, &currentTime);
}

static inline INT32 checkBFDPackageLength(char *buf)
{
	return ((buf[3] == BFD_PACKAGE_LENTH) ? TRUE : FALSE);
}

static inline void convertBFDPackageEndianToHost(BFDPackage_t *package)
{
	package->myDiscreaminator = ntohl(package->myDiscreaminator);
	package->yourDiscreaminator = ntohl(package->yourDiscreaminator);
	package->desiredMinTxInterval = ntohl(package->desiredMinTxInterval);
	package->requiredMinEchoRXInterval = ntohl(package->requiredMinEchoRXInterval);
	package->requiredMinRXInterval = ntohl(package->requiredMinRXInterval);
}
static inline void convertBFDPackageEndianToNet(BFDPackage_t *package)
{
	package->myDiscreaminator = htonl(package->myDiscreaminator);
	package->yourDiscreaminator = htonl(package->yourDiscreaminator);
	package->desiredMinTxInterval = htonl(package->desiredMinTxInterval);
	package->requiredMinEchoRXInterval = htonl(package->requiredMinEchoRXInterval);
	package->requiredMinRXInterval = htonl(package->requiredMinRXInterval);
}

static void printBFDPackage(BFDPackage_t *package)
{
	if (package == NULL)
	{
		return;
	}
	printf_s("\n\nBFD Info:");
	printf_s("\nVersion: %d ", package->version);
	printf_s("\nDiag: %d ", package->diagnostic);
	printf_s("\nSession State: %d ", ((UINT32)(package->state)));
}
//SOCKET SocketS49152;
static void WINAPI recivePackageHanldThread(void *arg)
{
	UDPPackage_t *udpPackage = NULL;
	BFDPackage_t *bfdPackage = NULL;
	if (arg == NULL)
	{
		return;
	}
	udpPackage = (UDPPackage_t *)arg;
	bfdPackage = &(udpPackage->BFDPackage);

	convertBFDPackageEndianToHost(bfdPackage);
	/*���м��*/
	if (bfdPackage->version!=BFD_VERSION)
	{
		return;
	}
	if (bfdPackage->detectMult==0)
	{
		return;
	}
	if (bfdPackage->myDiscreaminator==0)
	{
		return;
	}
	if (bfdPackage->yourDiscreaminator == 0 && (!(bfdPackage->state == SESSION_STATE_DOWN || bfdPackage->state == SESSION_STATE_ADMIN_DOWN)))
	{
		return;
	}

	SessionNode_t *tem = sessionListHead;
	while (tem != NULL)
	{
		if ((bfdPackage->yourDiscreaminator == tem->localDiscreaminator) || ((bfdPackage->yourDiscreaminator == 0))\
			&& (memcmp(&(udpPackage->peerAddress.sin_addr), &(tem->destinationIP), sizeof(IN_ADDR)) == 0))
		{
			BFDPackage_t *msgBfdPackage = (BFDPackage_t *)malloc(sizeof(BFDPackage_t));
			if (msgBfdPackage != NULL)
			{
				memcpy(msgBfdPackage, bfdPackage, sizeof(BFDPackage_t));
				memcpy(&(tem->localIP), &(udpPackage->localAddress), sizeof(IN_ADDR));
				PostThreadMessage(tem->threadID, MESSAGE_TYPE_DATA_RECIVE, (WPARAM)msgBfdPackage, NULL);
				break;
			}
		}
		tem = tem->next;
	}
	free_z(arg);
	return;
}

static void WINAPI bfdTimerPeriodicCallback_1ms(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2)
{
	if (sessionListHead == NULL)
	{
		return;
	}
	SessionNode_t *tem = sessionListHead;
	while (tem != NULL)
	{
		tem->remainTxTime -= 1000;
		if (tem->remainTxTime <= 0)
		{
			tem->remainTxTime = 0;
			/*post���ܻ�ʧ��,���Ǿ���ʧ�ܶ�ʱ�����ظ�����Ҳ��ʧ��,���Բ���ʧ��������*/
			PostThreadMessage(tem->threadID, MESSAGE_TYPE_TX_TIMEOUT, NULL, NULL);
		}
		if (tem->sessionState == SESSION_STATE_UP)
		{
			tem->remainRxTime -= 1000;
			if (tem->remainRxTime <= 0)
			{
				PostThreadMessage(tem->threadID, MESSAGE_TYPE_RX_TIMEOUT, NULL, NULL);
			}
		}
		tem = tem->next;
	}
}

static INT32 bfdTimerInit(void)
{
	MMRESULT timerID;
	timerID = timeSetEvent(1, 1, (LPTIMECALLBACK)bfdTimerPeriodicCallback_1ms, DWORD(1), TIME_PERIODIC);
	if (timerID == NULL)
	{
		return FALSE;
	}
	return TRUE;
}

static void WINAPI reciveThread(void *arg)
{
	char recvBuf[BFD_IP_PACKAGE_SIZE];/*��udp���յ����ݵ�buffer*/
	SOCKET SocketSrv;
	SOCKADDR_IN addrClient;
	SOCKADDR_IN addrServer;
	INT32 structLen = sizeof(addrClient);
	INT32 reciveBytes = 0;
	HANDLE uselessHandle;/*û���ô�,�����̵߳�ʱ����һ��*/

	memset(&uselessHandle, 0, sizeof(uselessHandle));
	memset(&addrClient, 0, sizeof(addrClient));
	memset(&addrServer, 0, sizeof(addrServer));
	memset(recvBuf, 0, sizeof(recvBuf));
	//SocketSrv = socket(AF_INET, SOCK_DGRAM, 0);
	SocketSrv = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
	if (SocketSrv == INVALID_SOCKET)
	{
		INT32 err = GetLastError();
		printf_s("\nerror : %d", err);
		if (err == 10013)
		{
			printf_s("\n��ʹ�ù���ԱȨ������");
		}
		EXIT();
	}
	addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(BFD_PORT);
	/*�󶨶˿ں��׽���*/
	if (bind(SocketSrv, (SOCKADDR*)&addrServer, sizeof(SOCKADDR)) != 0)
	{
		printf("\n�󶨶˿�ʧ��");
		EXIT();
	}
	while (1)
	{
		reciveBytes = recvfrom(SocketSrv, recvBuf, BFD_IP_PACKAGE_SIZE, 0, (SOCKADDR*)&addrClient, &structLen);
		//	reciveBytes = recv(SocketSrv, buffer, 1460, 0);
		if (reciveBytes <= 0)
		{
			//printf_s("\nSocket���մ���:%d", WSAGetLastError());
			continue;
		}
		if (reciveBytes != BFD_IP_PACKAGE_SIZE)
		{
			//printf_s("\n�յ�%d��СIP��,��֧��BFD��֤",reciveBytes);
			continue;
		}
		if (recvBuf[8] != (char)0xff)/*IP��ͷ��8�ֽ���TTL,�����Ϊ255,����*/
		{
			//printf_s("\n�յ�TTL= %d �ı���", recvBuf[8]);
			continue;
		}
		char *bfdData = &recvBuf[28];/*�ӵ�28���ֽڿ�ʼ��BFD����*/
		if (checkBFDPackageLength(bfdData))
		{
			UDPPackage_t *tem = (UDPPackage_t *)malloc(sizeof(UDPPackage_t));
			if (tem == NULL)
			{
				continue;
			}
			/*���յ��ı����е�Ŀ��IP��Ҳ���Ǳ���IP������һ��*/
			memcpy(&tem->localAddress.S_un.S_un_b, &recvBuf[16], 4);
			memcpy(&(tem->peerAddress), &addrClient, sizeof(tem->peerAddress));
			memcpy(&(tem->BFDPackage), bfdData, sizeof(tem->BFDPackage));
			uselessHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recivePackageHanldThread, (void *)tem, 0, NULL);
			if (uselessHandle != NULL)
			{
				CloseHandle(uselessHandle);
			}
			else
			{
				printf_s("\n�����߳�ʧ��");
				EXIT();/*�����߳�ʧ��*/
			}
		}
	}
	return;
}

static void bfdSessionListInsert(SessionNode_t *node)
{
	if (sessionListHead == NULL)
	{
		sessionListHead = node;
	}
	else
	{
		SessionNode_t *tem = sessionListHead;
		while (tem->next != NULL)
		{
			tem = tem->next;
		}
		tem->next = node;
	}
}

static INT32 bfdSessionListDelete(SessionNode_t *node)
{
	if (sessionListHead == NULL)
	{
		return FALSE;
	}
	if (sessionListHead == node)
	{
		sessionListHead = NULL;
		return TRUE;
	}

	SessionNode_t *current = sessionListHead->next;
	SessionNode_t *last = sessionListHead;

	while (TRUE)
	{
		if (current == NULL)
		{
			return FALSE;
		}
		else if (current == node)
		{
			last->next = current->next;
			break;
		}
		else
		{
			last = current;
			current = current->next;
		}
	}
	return TRUE;
}

static void WINAPI bfdSessionHandleTread(void *arg)
{//��̬����Ӧ���Ƶ�����
	if (arg == NULL)
	{
		printf_s("\nBFD�̴߳����쳣:����ΪNULL");
		return;
	}

	UDPPackage_t udpPackage;
	SessionArg_t *sessionArg = (SessionArg_t*)arg;
	SOCKET bfdSendSocket = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN localAddr;
	MSG msg;
	INT32 negotiatedTxTime = BFD_INIT_TIME;
	//INT32 negotiatedRxTime_ms = BFD_INIT_TIME;
	INT32 negotiatedCheckTime = BFD_INIT_TIME;
	BOOL threadShouldNotExit = TRUE;
	BOOL parameterHasChanged = FALSE;

	memset(&msg, 0, sizeof(msg));
	memset(&localAddr, 0, sizeof(localAddr));
	memset(&udpPackage, 0, sizeof(udpPackage));

	SessionNode_t *session = (SessionNode_t *)malloc(sizeof(SessionNode_t));
	if (session == NULL)
	{
		printf_s("\n�߳����ݷ���ʧ��");
		return;
	}
	memset(session, 0, sizeof(SessionNode_t));
	memcpy(&(udpPackage.peerAddress.sin_addr), &(sessionArg->destinationIP), sizeof(udpPackage.peerAddress.sin_addr));
	udpPackage.peerAddress.sin_family = AF_INET;
	udpPackage.peerAddress.sin_port = htons(BFD_PORT);

	localAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	localAddr.sin_family = AF_INET;

	INT32 ttl = 255;
	setsockopt(bfdSendSocket, IPPROTO_IP, IP_TTL, (char *)&ttl, sizeof(ttl));

	memcpy(&(session->destinationIP), &(sessionArg->destinationIP), sizeof(IN_ADDR));

	session->next = NULL;
	session->threadID = GetCurrentThreadId();
	session->remainTxTime = negotiatedTxTime;
	session->sessionState = SESSION_STATE_DOWN;
	session->rawRxTime = sessionArg->reciveInterval;
	session->rawTxTime = sessionArg->transmitInterval;
	session->remoteRxTime = BFD_INIT_TIME;
	session->remoteTxTime = BFD_INIT_TIME;

	WaitForSingleObject(hSessionMutex, INFINITE);
	session->localDiscreaminator = localDiscreaminator;
	localAddr.sin_port = htons(currentPort);
	currentPort >= UINT16_MAX ? (currentPort = BFD_SEND_PORT_INIT) : currentPort++;
	while (bind(bfdSendSocket, (SOCKADDR*)&localAddr, sizeof(localAddr)) != 0)
	{
		localAddr.sin_port = htons(currentPort);
		currentPort >= UINT16_MAX ? (currentPort = BFD_SEND_PORT_INIT) : currentPort++;
	}
	ReleaseMutex(hSessionMutex);

	udpPackage.BFDPackage.desiredMinTxInterval = BFD_INIT_TIME;
	udpPackage.BFDPackage.requiredMinRXInterval = BFD_INIT_TIME;
	udpPackage.BFDPackage.state = session->sessionState;
	udpPackage.BFDPackage.myDiscreaminator = session->localDiscreaminator;
	udpPackage.BFDPackage.detectMult = sessionArg->multiplier;
	udpPackage.BFDPackage.yourDiscreaminator = 0x00;
	udpPackage.BFDPackage.length = BFD_PACKAGE_LENTH;
	udpPackage.BFDPackage.version = BFD_VERSION;
	convertBFDPackageEndianToNet(&(udpPackage.BFDPackage));
	bfdSessionListInsert(session);

	time_t currentTime;
	time(&currentTime);
	struct tm timeinfo;
	localtime_s(&timeinfo, &currentTime);
	printf_s("\n*%s %d %d:%d:%d: bfd_session_created, neigh %s , handle:%d ", month[timeinfo.tm_mon], \
		timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, \
		inet_ntoa(udpPackage.peerAddress.sin_addr), session->localDiscreaminator);
	free(sessionArg);
	while (threadShouldNotExit)
	{
		while (GetMessage(&msg, NULL, 0, 0) == -1)
		{
			//printf_s("��ȡ��Ϣʧ��: %d", GetLastError());
		}
		//printf_s("\n rx=%d,tx=%d", negotiatedCheckTime, negotiatedTxTime);
		if (msg.message == MESSAGE_TYPE_TX_TIMEOUT)
		{
			session->remainTxTime = negotiatedTxTime;
			sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
		}
		else if (msg.message == MESSAGE_TYPE_RX_TIMEOUT)
		{
			session->sessionState = SESSION_STATE_DOWN;
			convertBFDPackageEndianToHost(&(udpPackage.BFDPackage));
			udpPackage.BFDPackage.state = SESSION_STATE_DOWN;
			udpPackage.BFDPackage.diagnostic = DIAGNOSTIC_CONTROL_DETECTION_TIME_EXPIRED;
			udpPackage.BFDPackage.desiredMinTxInterval = BFD_INIT_TIME;
			udpPackage.BFDPackage.requiredMinRXInterval = BFD_INIT_TIME;
			udpPackage.BFDPackage.yourDiscreaminator = 0;
			session->remainRxTime = BFD_INIT_TIME;
			session->remainTxTime = BFD_INIT_TIME;

			struct tm timeinfo;
			getCurrentTime(&timeinfo);
			char destIP[16];
			memset(destIP, '\0', sizeof(destIP));
			strcpy_s(destIP, inet_ntoa(session->destinationIP));
			if (udpPackage.BFDPackage.diagnostic < MAX_DIAGNOSTIC_NUMBER)
			{
				sprintf_s(bfdStateChangeString, BFD_STR_DOWN, month[timeinfo.tm_mon], \
					timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, destIP, \
					inet_ntoa(session->localIP), session->localDiscreaminator, bfdDiagnosticReason[udpPackage.BFDPackage.diagnostic]);
				printf_s("%s", bfdStateChangeString);
			}
			convertBFDPackageEndianToNet(&(udpPackage.BFDPackage));
			sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
		}
		else if (msg.message == MESSAGE_TYPE_DATA_RECIVE)
		{
			/*Э������
			*��⵽down,�����ظ�init
			*�Զ˻�ֱ�ӻظ�POLL
			*�����ظ�Final
			*�Զ������ظ�����Flag�ı���
			*�������ͱ���POLL
			*�Զ���������Final
			*�յ�Final�Ժ������ظ�һ������Flag�ı���
			*
			*�����Ժ�����ݸ���ͬ��,�յ�POLL�����ظ�Final,�յ�Final�����ظ�����flag�ı���
			*/

			BFDPackage_t *recvieBfdPackage = (BFDPackage_t*)msg.wParam;

			/*������˶���UP������Ƿ���Poll��Final*/
			if ((recvieBfdPackage->state == SESSION_STATE_UP) && (session->sessionState == SESSION_STATE_UP))
			{
				session->remainRxTime = negotiatedCheckTime;
				/*
				BFD���Ʊ��ķ���ʱ����Ϊ����Desired Min TX Interval��Զ�Required Min RX
				Interval֮�е����ֵ��Ҳ����˵�Ƚ�����һ�������˷���Ƶ�ʡ�
				���ʱ��Ϊ�Զ�BFD���Ʊ����е�Detect Mult���Ծ���Э�̵ĶԶ�BFD���Ʊ��ķ���ʱ
				������
				����Ӵ󱾶�Desired Min TX Interval����ô����ʵ�ʷ���BFD���Ʊ��ĵ�ʱ������
				��Ҫ���յ��Զ�F�ֶ���λ�ı��ĺ���ܸı䣬����Ϊ��ȷ���ڱ��˼Ӵ�BFD���Ʊ��ķ�
				��ʱ����ǰ�Զ��Ѿ��Ӵ��˼��ʱ�䣬������ܵ��¶Զ˼�ⶨʱ������ʱ��
				�����С����Required Min RX Interval����ô���˼��ʱ�����Ҫ���յ��Զ�F�ֶ���
				λ�ı��ĺ���ܸı䣬����Ϊ��ȷ���ڱ��˼�С���ʱ��ǰ�Զ��Ѿ���С��BFD���Ʊ�
				�ķ��ͼ��ʱ�䣬������ܵ��±��˼�ⶨʱ������ʱ��
				Ȼ�������СDesired Min TX Interval������BFD���Ʊ��ķ���ʱ��������������
				С���Ӵ�Required Min RX Interval�����˼��ʱ�佫�������Ӵ�
				*/
				if ((recvieBfdPackage->flagPoll == SET) || (recvieBfdPackage->flagFinal == SET))
				{
					/*����Է�Poll��λ,���������,Ȼ��Finall��λ,��������һ��Package*/
					/*ÿ���յ�final��Poll�������еĲ�������һ��,��������*/
					convertBFDPackageEndianToHost(&(udpPackage.BFDPackage));
					/*��ʱ��Э��*/
					negotiatedTxTime = MAX(session->rawTxTime, recvieBfdPackage->requiredMinRXInterval);
					negotiatedCheckTime = MAX(session->rawRxTime, recvieBfdPackage->desiredMinTxInterval)*recvieBfdPackage->detectMult;/*�öԶ˵ļ�ⱶ��*/
					udpPackage.BFDPackage.desiredMinTxInterval = session->rawTxTime;
					udpPackage.BFDPackage.requiredMinRXInterval = session->rawRxTime;
					if (recvieBfdPackage->flagPoll == SET)
					{
						udpPackage.BFDPackage.flagFinal = SET;
						session->remoteRxTime = recvieBfdPackage->requiredMinRXInterval;
						session->remoteTxTime = recvieBfdPackage->desiredMinTxInterval;
						session->remoteMult = recvieBfdPackage->detectMult;
					}
					if (recvieBfdPackage->flagFinal == SET)
					{
						parameterHasChanged = FALSE;
						udpPackage.BFDPackage.flagPoll = RESET;
					}
					convertBFDPackageEndianToNet(&udpPackage.BFDPackage);
					/*�κ�����¶������ظ�һ������*/
					sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
					udpPackage.BFDPackage.flagFinal = RESET;/*�����������ֽ���Ӱ���޸ı�־λ*/
				}
			}
			else if (recvieBfdPackage->state == SESSION_STATE_DOWN)
			{
				convertBFDPackageEndianToHost(&(udpPackage.BFDPackage));
				/*����յ�һ��Զ�˱���Ϊdown,״̬�任��INIT*/
				session->sessionState = SESSION_STATE_INIT;
				udpPackage.BFDPackage.state = session->sessionState;
				udpPackage.BFDPackage.yourDiscreaminator = recvieBfdPackage->myDiscreaminator;
				convertBFDPackageEndianToNet(&(udpPackage.BFDPackage));
				sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
			}
			else if ((recvieBfdPackage->state == SESSION_STATE_INIT) || (recvieBfdPackage->state == SESSION_STATE_UP&&session->sessionState == SESSION_STATE_INIT))
			{
				convertBFDPackageEndianToHost(&(udpPackage.BFDPackage));
				/*����յ�һ��Զ��ΪINIT�ı���,��ֱ��Ǩ�Ƶ�UP*/
				if (session->sessionState == SESSION_STATE_DOWN)
				{
					/*�����ǰ��״̬ΪDOWN,��Ҫ��¼�Է���Discreaminator*/
					udpPackage.BFDPackage.yourDiscreaminator = recvieBfdPackage->myDiscreaminator;
				}
				if (recvieBfdPackage->flagPoll == SET)
				{
					/*����Զ�Poll��1,��ظ�Final*/
					udpPackage.BFDPackage.flagFinal = SET;
				}

				udpPackage.BFDPackage.flagPoll = SET;
				parameterHasChanged = TRUE;
				udpPackage.BFDPackage.desiredMinTxInterval = session->rawTxTime;
				udpPackage.BFDPackage.requiredMinRXInterval = session->rawRxTime;
				udpPackage.BFDPackage.state = SESSION_STATE_UP;
				udpPackage.BFDPackage.diagnostic = DIAGNOSTIC_NO_DIAGNOSTIC;

				session->sessionState = SESSION_STATE_UP;
				negotiatedTxTime = MAX(session->rawTxTime, recvieBfdPackage->requiredMinRXInterval);
				negotiatedCheckTime = MAX(session->remainRxTime, recvieBfdPackage->desiredMinTxInterval)*recvieBfdPackage->detectMult;/*�öԶ˵ļ�ⱶ��*/
				session->remainRxTime = negotiatedCheckTime;
				session->remainTxTime = negotiatedTxTime;

				struct tm timeinfo;
				getCurrentTime(&timeinfo);

				char destIP[16];
				memset(destIP, '\0', sizeof(destIP));
				strcpy_s(destIP, inet_ntoa(session->destinationIP));
				sprintf_s(bfdStateChangeString, BFD_STR_UP, month[timeinfo.tm_mon], \
					timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, \
					destIP, inet_ntoa(session->localIP), \
					session->localDiscreaminator);
				printf_s("%s", bfdStateChangeString);

				convertBFDPackageEndianToNet(&(udpPackage.BFDPackage));
				sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
				udpPackage.BFDPackage.flagFinal = RESET;
			}
			else if (recvieBfdPackage->state == SESSION_STATE_ADMIN_DOWN)
			{
				convertBFDPackageEndianToHost(&udpPackage.BFDPackage);
				if (session->sessionState==SESSION_STATE_UP)
				{
					session->sessionState = SESSION_STATE_DOWN;
					udpPackage.BFDPackage.state = SESSION_STATE_DOWN;
					udpPackage.BFDPackage.diagnostic = DIAGNOSTIC_NEIGHBOR_SIGNALED_SESSION_DOWN;
				}
				convertBFDPackageEndianToNet(&udpPackage.BFDPackage);
			}
			free((void *)(msg.wParam));
		}
		else if (msg.message == MESSAGE_TYPE_PARAMETER_UPDATA)
		{
			SessionArg_t *updata = (SessionArg_t *)msg.wParam;
			if (arg == NULL)
			{
				continue;
			}
			session->rawRxTime = updata->reciveInterval;
			session->rawTxTime = updata->transmitInterval;
			convertBFDPackageEndianToHost(&udpPackage.BFDPackage);
			udpPackage.BFDPackage.requiredMinRXInterval = updata->reciveInterval;
			udpPackage.BFDPackage.desiredMinTxInterval = updata->transmitInterval;
			udpPackage.BFDPackage.detectMult = updata->multiplier;

			/*��ȻRFC5880��˵�޸ļ�ⱶ����ʱ��the use of a Poll Sequence is not necessary
			ʵ����cisco��û��POLL��������޸ļ�ⱶ����cisco������¼��ʱ��
			*/
			if (session->sessionState == SESSION_STATE_UP)
			{
				parameterHasChanged = TRUE;
				udpPackage.BFDPackage.flagPoll = 1;
				if (updata->reciveInterval > session->rawRxTime)
				{
					/*�Ӵ�Required Min RX Interval�����˼��ʱ�佫�������Ӵ�*/
					negotiatedCheckTime = MAX(updata->reciveInterval, session->remoteTxTime)*session->remoteMult;/*�öԶ˵ļ�ⱶ��*/
				}
				if (updata->transmitInterval < session->rawTxTime)
				{
					/*��СDesired Min TX Interval������BFD���Ʊ��ķ���ʱ��������������С��*/
					negotiatedTxTime = MAX(session->rawTxTime, updata->transmitInterval);
				}
			}
			convertBFDPackageEndianToNet(&udpPackage.BFDPackage);
			if (session->sessionState == SESSION_STATE_UP)
			{
				sendto(bfdSendSocket, (char *)&(udpPackage.BFDPackage), BFD_PACKAGE_LENTH, 0, (SOCKADDR *)&(udpPackage.peerAddress), sizeof(udpPackage.peerAddress));
			}

			struct tm timeinfo;
			getCurrentTime(&timeinfo);
			printf_s("\n*%s %d %d:%d:%d: bfd config apply.", month[timeinfo.tm_mon], \
				timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
		}
		else if (msg.message == MESSAGE_TYPE_SESSION_CANCEL)
		{
			threadShouldNotExit = FALSE;
			continue;
		}
	}
	bfdSessionListDelete(session);
	free(session);
}

__declspec(dllexport) INT32 bfdCreatBFDSession(char *peerip_str, char *reciveInterval_str, char *transmitInterval_str, char *multiplier_str)
{
	if (peerip_str == NULL || transmitInterval_str == NULL || reciveInterval_str == NULL || multiplier_str == NULL)
	{
		return FALSE;
	}
	if (BFDhasInitialized == FALSE)
	{
		HANDLE tem = bfdInit();
		CloseHandle(tem);
		BFDhasInitialized = TRUE;
	}

	IN_ADDR destinationIP;
	destinationIP.S_un.S_addr = inet_addr(peerip_str);
	SessionArg_t *sessionArg = (SessionArg_t*)malloc(sizeof(SessionArg_t));
	if (sessionArg == NULL)
	{
		return FALSE;
	}
	INT32 reciveInterval = atoi(reciveInterval_str);
	if (reciveInterval > (MAX_RX_INTERVAL / 1000) || reciveInterval < (MIN_RX_INTERVAL / 1000))
	{
		printf_s("\n Recive Interval Error, min:%d max:%d\n", MIN_RX_INTERVAL / 1000, MAX_RX_INTERVAL / 100);
		return FALSE;
	}
	INT32 transmitInterval = atoi(transmitInterval_str);
	if (transmitInterval > (MAX_TX_INTERVAL / 1000) || transmitInterval < (MIN_TX_INTERVAL / 1000))
	{
		printf_s("\n transmit Interval Error, min:%d max:%d\n", MIN_TX_INTERVAL / 1000, MAX_TX_INTERVAL / 1000);
		return FALSE;
	}
	INT32 multiplier = atoi(multiplier_str);
	if (multiplier > (MAX_MULT) || transmitInterval < (MIN_MULT))
	{
		printf_s("\n Multiplier Error, min:%d max:%d\n", MIN_MULT, MAX_MULT);
		return FALSE;
	}

	sessionArg->destinationIP = destinationIP;
	sessionArg->reciveInterval = reciveInterval * 1000;
	sessionArg->transmitInterval = transmitInterval * 1000;
	sessionArg->multiplier = multiplier;

	SessionNode_t *sessionWalk = sessionListHead;
	while (sessionWalk != NULL)
	{
		if (memcmp(&(sessionWalk->destinationIP), &(sessionArg->destinationIP), sizeof(IN_ADDR)) == 0)
		{
			PostThreadMessage(sessionWalk->threadID, MESSAGE_TYPE_PARAMETER_UPDATA, (WPARAM)sessionArg, NULL);
			return TRUE;
		}
		sessionWalk = sessionWalk->next;
	}

	HANDLE threadHandle;
	threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)bfdSessionHandleTread, sessionArg, 0, NULL);
	if (threadHandle != NULL)
	{
		CloseHandle(threadHandle);
	}
	else
	{
		printf_s("\n�����߳�ʧ��");
		EXIT();/*�����߳�ʧ��*/
	}
	return TRUE;
}
__declspec(dllexport)INT32 bfddeleteBFDSession(char *peerip_srt)
{
	return TRUE;
}

__declspec(dllexport) INT32 socketInit(void)
{
	INT16 wVersion = MAKEWORD(2, 2);
	WSADATA wsaData;
	INT32 err = WSAStartup(wVersion, &wsaData);

	if (err != 0)
	{
		printf("\nWSAStartup failed");
		EXIT();
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("\ncould not find a usable version of Winsock.dll");
		WSACleanup();
		EXIT();
	}
	return TRUE;
}

__declspec(dllexport) HANDLE bfdInit(void)
{
	hSessionMutex = CreateMutex(NULL, FALSE, NULL);
	if (bfdTimerInit() == FALSE)
	{
		printf("\nTimer Init failed!");
		EXIT();
	}
	HANDLE handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reciveThread, NULL, 0, NULL);
	if (handle == NULL)
	{
		printf("\nCreate recive thread failed!");
		EXIT();
	}
	return handle;
}
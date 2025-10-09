#pragma once
#include "Util.h"

//通用包结构
#pragma pack(1)
typedef struct {
	char	protocl[8];		//协议名字
	char	detailType[4];	//协议格式  TXT、GZIP等
	char	data[0];		//协议内容
}
cc_client_packet_t;
#pragma pack()


struct ClientDataInfo
{
	CBufferPtr dataBuffer;//数据缓冲区
	CONNID dwConnid;//连接ID
};

class CTcpPullClientImpl : public CTcpPullClientListener
{
public:
	CTcpPullClientImpl(const CStringA& strClientID);
	~CTcpPullClientImpl();
	//连接到服务器
	BOOL StartClient(const CStringA& strServerHostName, short usPort);
	//发送心跳包
	BOOL SendHeartBeat();
	void Stop();
public:
	virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID);
	virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, int iLength);
	virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	CStringA GetIPByDomain(const char* szHostName);
	//判断数据包是否完整，如果完整返回完整数据包的长度，否则返回-1
	static int get_length(const BYTE* bData, int nDataLen)
	{
		if (nDataLen <= 0)
			return -1;
		string strData((LPCSTR)bData, nDataLen);
		int iIndex = strData.find(s_strProtoDelimiter);
		if (iIndex == string::npos)
		{
			return -1;
		}
		else
		{
			return iIndex + s_strProtoDelimiter.GetLength();
		}
	}
	//解析数据包
	void Parse(const BYTE* bData, int nDataLen, CONNID dwConnID);	
	void SetClientState(EnAppState state);
	//统计的本次连接的行情数量
	int m_tickCount;
	//最近一次连接成功的时间
	static time_t s_tLastSuc;
	//最近一次连接失败的时间
	static time_t s_tLastFail;
	//最近一次断线的时间
	static time_t s_tLastClose;
	//当天已发送断线的总条数
	static int s_iDXTotalNum;
	//底层的tcp数据队列
	static vector<TSocketTask*> s_queueTCPData;
	static std::mutex s_lockTCPData;
	static std::condition_variable s_cvTCPData;
	//处理底层数据的线程函数
	static void HandleTCPDateProc(PVOID arg);
private:
	//接受缓冲区，用于存储一个完整包
	CBufferPtr* m_pRecvBuffer;

	CTcpPullClientPtr m_Client;
	CStringA m_strClientID;
public:
	volatile time_t m_tActiveTime;		//最近一次收到心跳回应的时间	
	volatile EnAppState m_enClientState;
	volatile bool m_bRun = true;
};


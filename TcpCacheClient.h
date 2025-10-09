#pragma once
#include "Util.h"
#include <atomic>
//包命令
typedef enum {
	cmd_KLINESERVER_HEARTBEAT = 0,				//心跳
	cmd_KLINESERVER_HEARTBEAT_SUC = 1,			//心跳成功
	cmd_KLINE_COUNT_REQ = 2,					//按照截止时间戳,向前取给定根数的K线缓存
	cmd_KLINE_TIME_REQ = 3,						//按照起止时间请求K线缓存
	cmd_KLINE_CACHE_RSP = 4						//K线缓存请求的应答
} cache_cmd_t;

//通用包结构
#pragma pack(push,1)
typedef struct {
	unsigned char	cmd;	//	command, see cc_cmd_t
	UINT32			size;	//	data size in consequent bytes 
	unsigned char	data[0];//	packet payload
}
cache_packet_t;
#pragma pack(pop)

//struct CacheClientDataInfo
//{
//	CBufferPtr dataBuffer;//数据缓冲区
//	CONNID dwConnid;//连接ID
//};

class CTcpCacheClient : public CTcpPullClientListener
{
public:
	CTcpCacheClient();
	~CTcpCacheClient();
	//连接到服务器
	BOOL StartClient(const CStringA& strServerHostName, short usPort);
	//发送心跳包
	BOOL SendHeartBeat();
	//向缓存服务器发送获取指定根数K线的请求
	BOOL SendKLineCountReq(const OptionID& optionid, UINT32 iEndTimeStamp, UINT32 iCount, KLineType klineType, UINT32& iReqId);
	//向缓存服务器发送获取指定起始时间K线的请求
	BOOL SendKLineTimeReq(const OptionID& optionid, UINT32 iStartTimeStamp, UINT32 iEndTimeStamp, KLineType klineType, UINT32& iReqId);
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
		if (nDataLen < sizeof(cache_packet_t))
			return -1;

		cache_packet_t* packet = (cache_packet_t*)bData;
		UINT32 iPacketSize = ntohl(packet->size);
		if ((UINT32)nDataLen >= iPacketSize + sizeof(cache_packet_t))
		{
			return iPacketSize + sizeof(cache_packet_t);
		}
		else
		{
			return -1;
		}
	}
	//解析数据包
	void Parse(const BYTE* bData, int nDataLen, CONNID dwConnID);	
	void SetClientState(EnAppState state);

	//与缓存服务器通信时,自己生成的客户端id
	static atomic<UINT32> s_iCacheReqId;
	//最近一次连接成功的时间
	static time_t s_tLastSuc;
	//最近一次连接失败的时间
	static time_t s_tLastFail;
	//最近一次断线的时间
	static time_t s_tLastClose;
	//当天已发送断线的总条数
	static int s_iDXTotalNum;
private:
	//接受缓冲区，用于存储一个完整包
	CBufferPtr* m_pRecvBuffer;

	CTcpPullClientPtr m_Client;
public:
	//最近一次收到心跳回应的时间
	volatile time_t m_tActiveTime;		
	volatile EnAppState m_enClientState;
	volatile bool m_bRun = true;
};


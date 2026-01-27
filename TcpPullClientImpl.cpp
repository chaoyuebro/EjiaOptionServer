#include "TcpPullClientImpl.h"
#include <iostream>

#define SAFEDELETE(pObject) if( pObject != NULL ) {delete pObject; pObject = NULL;}

extern bool g_bRun;

//最近一次连接成功的时间
time_t CTcpPullClientImpl::s_tLastSuc = 0;
//最近一次连接失败的时间
time_t CTcpPullClientImpl::s_tLastFail = 0;
//最近一次断线的时间
time_t CTcpPullClientImpl::s_tLastClose = 0;
//当天已发送断线的总条数
int CTcpPullClientImpl::s_iDXTotalNum = 0;
//底层的tcp数据队列
vector<TSocketTask*> CTcpPullClientImpl::s_queueTCPData;
std::mutex CTcpPullClientImpl::s_lockTCPData;
std::condition_variable CTcpPullClientImpl::s_cvTCPData;

CTcpPullClientImpl::CTcpPullClientImpl(const CStringA& strClientID)
	: m_pRecvBuffer(NULL), m_Client(this), m_tActiveTime(0)
	, m_strClientID(strClientID), m_tickCount(0)
{
}

CTcpPullClientImpl::~CTcpPullClientImpl()
{	
	Stop();
	//通知数据处理线程停止工作并退出
	m_bRun = false;
}

CStringA CTcpPullClientImpl::GetIPByDomain(const char* szHostName)
{
	char cIp[32] = { 0 };
	struct hostent * pHost;
	int i;
	pHost = gethostbyname(szHostName);
	for (i = 0; pHost != NULL && pHost->h_addr_list[i] != NULL; i++)
	{
		LPCSTR psz = inet_ntoa(*(struct in_addr *)pHost->h_addr_list[i]);
		memset(cIp, 0x00, 32);
		memcpy(cIp, psz, strlen(psz));
	}
	return cIp;
}

//连接到Tick行情服务,获取合约信息并订阅行情
BOOL CTcpPullClientImpl::StartClient(const CStringA& strServerHostName, short usPort)
{
	CString strLog;
	USES_CONVERSION;
	//CString strServerIp = A2T(GetIPByDomain(strServerHostName));
	//if (strServerIp.IsEmpty())
	//{
	//	strLog.Format(_T("连接脚本中控失败: %s域名解析失败!"), A2T(strServerHostName));
	//	g_Logger->info(strLog);
	//	return FALSE;
	//}
	SetClientState(ST_STARTING);
	time_t tNow = ::time(NULL);
	if (m_Client->Start(A2T(strServerHostName), usPort, FALSE))
	{
		SetClientState(ST_STARTED);
		CBufferPtr bufferReq;
		//发送登陆请求
		CStringA strLoginData;
		//发送登陆包
		strLoginData.Format("LGIN    TXT %s%s", (LPCSTR)m_strClientID, (LPCSTR)s_strProtoDelimiter);
		bufferReq.Cat((const BYTE*)(LPCSTR)strLoginData, strLoginData.GetLength());
		if (m_Client->Send(bufferReq.Ptr(), bufferReq.Size()))
		{
			g_Logger->info(_T("已连接到行情转发服务{}:{}"), (LPCSTR)strServerHostName, usPort);
			//初始化心跳活跃时间
			m_tActiveTime = tNow;
			//发送连接成功的短信通知
			if (g_bSendSms && tNow - s_tLastSuc >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
			{
				++s_iDXTotalNum;
				SendSms("连接期权行情转发服务成功!");
				s_tLastSuc = tNow;
			}
			return TRUE;
		}
		else
		{			
			strLog.Format(_T("到行情转发服务的登录请求包发送失败: %d|%s"), ::SYS_GetLastError(), ::HP_GetSocketErrorDesc(SE_DATA_SEND));
			g_Logger->info(strLog);			
			m_Client->Stop();
			SetClientState(ST_STOPPED);
			return FALSE;
		}
	}
	else
	{
		//发送连接失败的短信通知
		if (g_bSendSms && tNow - s_tLastFail >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
		{
			++s_iDXTotalNum;
			SendSms("连接期权行情转发服务失败!");
			s_tLastFail = tNow;
		}
		SetClientState(ST_STOPPED);
		strLog.Format(_T("连接到期权行情转发服务失败: %d|%s"), m_Client->GetLastError(), m_Client->GetLastErrorDesc());
		g_Logger->info(strLog);		
		return FALSE;
	}
}

void CTcpPullClientImpl::Stop()
{
	if (m_enClientState == ST_STARTED || ST_STARTING == m_enClientState)
	{
		SetClientState(ST_STOPPING);
		VERIFY(m_Client->Stop());
		SetClientState(ST_STOPPED);
		g_Logger->info("客户端已停止!");
	}
}

void CTcpPullClientImpl::SetClientState(EnAppState state)
{
	m_enClientState = state;
	if (ST_STOPPED == state)
	{
		//将连接活跃时间设置为0.以便尽快触发断线重连
		m_tActiveTime = 0;
	}
}

EnHandleResult CTcpPullClientImpl::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
	return HR_OK;
}

EnHandleResult CTcpPullClientImpl::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	return HR_OK;
}
//pTask需要手动销毁
void CALLBACK HandleDataProc(TSocketTask* pTask)
{
	CTcpPullClientImpl* pThis = (CTcpPullClientImpl*)pTask->sender;
	if (pThis)
	{
		pThis->Parse(pTask->buf, pTask->bufLen, pTask->connID);
		::HP_Destroy_SocketTaskObj(pTask);
	}
}
//处理底层数据的线程函数
void CTcpPullClientImpl::HandleTCPDateProc(PVOID arg)
{
	while (true)
	{
		vector<TSocketTask*> queueDatas;
		//等待生产者数据到达
		std::unique_lock<std::mutex> mlock(s_lockTCPData);
		s_cvTCPData.wait(mlock);		
		s_queueTCPData.swap(queueDatas);
		mlock.unlock();
		//处理数据
		if (queueDatas.size() > 0)
		{
			for (TSocketTask* pSocketTask : queueDatas)
				HandleDataProc(pSocketTask);
		}
	}
}
EnHandleResult CTcpPullClientImpl::OnReceive(ITcpClient* pSender, CONNID dwConnID, int iLength)
{	
	CBufferPtr buffer(iLength);
	EnFetchResult result = ITcpPullClient::FromS(pSender)->Fetch(buffer, (int)buffer.Size());
	BYTE* pData2 = (BYTE*)buffer.Ptr();
	int iLength2 = iLength;
	if (result == FR_OK)
	{
		if (NULL == m_pRecvBuffer)//新连接
		{
			//将所有完整的包数据添加进数据队列
			while (true)
			{
				int nTotalLen = get_length(pData2, iLength2);
				if (nTotalLen > 0)//收到一个完整包
				{
					//将该完整包添加到数据队列
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleDataProc, this, dwConnID, pData2, nTotalLen);
					//通知消费者处理数据
					{
						std::lock_guard<std::mutex> guard(s_lockTCPData);
						s_queueTCPData.emplace_back(pTask);
						s_cvTCPData.notify_one();
					}
					//继续检测是否有完整包
					pData2 += nTotalLen;
					iLength2 -= nTotalLen;
				}
				else//数据包不完整，则存储起来
				{
					m_pRecvBuffer = new CBufferPtr(pData2, iLength2);					
					break;
				}
			}
		}
		else
		{
			m_pRecvBuffer->Cat(pData2, iLength2);
			//将所有完整的包数据添加进数据队列
			pData2 = m_pRecvBuffer->Ptr();
			iLength2 = m_pRecvBuffer->Size();
			while (true)
			{
				int nTotalLen = get_length(pData2, iLength2);
				//收到一个完整包
				if (nTotalLen > 0)
				{
					//将该完整包添加到数据队列
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleDataProc, this, dwConnID, pData2, nTotalLen);
					//通知消费者处理数据
					{
						std::lock_guard<std::mutex> guard(s_lockTCPData);
						s_queueTCPData.emplace_back(pTask);
						s_cvTCPData.notify_one();
					}
					//继续检测是否有完整包
					pData2 += nTotalLen;
					iLength2 -= nTotalLen;
				}
				else//数据包不完整，判断是否需要截断数据并保存
				{
					if (iLength2 != m_pRecvBuffer->Size())
					{
						//将处理完成的数据剔除掉
						CBufferPtr* ptr = new CBufferPtr(pData2, iLength2);
						//删除之前的缓冲区对象
						SAFEDELETE(m_pRecvBuffer);
						m_pRecvBuffer = ptr;
					}
					break;
				}
			}
		}
	}
	return HR_OK;
}

EnHandleResult CTcpPullClientImpl::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
	CString strLog;
	strLog.Format(_T("%s-->与行情转发服务的连接关闭."), _T(__FUNCTION__));
	g_Logger->info(strLog);
	SetClientState(ST_STOPPED);
	//发送连接关闭的短信通知
	time_t tNow = ::time(NULL);
	if (g_bSendSms && tNow - s_tLastClose >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
	{
		++s_iDXTotalNum;
		SendSms("与期权行情转发服务的连接断开!");
		s_tLastClose = tNow;
	}
	return HR_OK;
}
bool isTimeBetween4pmAnd8pm() {
	// 获取当前时间点
	auto now = std::chrono::system_clock::now();

	// 转换为time_t类型
	std::time_t current_time = std::chrono::system_clock::to_time_t(now);

	// 转换为本地时间结构
	std::tm* local_time = std::localtime(&current_time);

	// 获取当前小时
	int current_hour = local_time->tm_hour;
	//cout << current_hour << endl;
	// 判断是否在下午4点(16:00)到下午8点(20:00)之间
	return (current_hour >= 16 && current_hour < 20);
}

//解析数据包
void CTcpPullClientImpl::Parse(const BYTE* bData, int nDataLen, CONNID dwConnID)
{	
	//只要有数据就更新最近活跃时间
	m_tActiveTime = ::time(NULL);

	cc_client_packet_t* packet = (cc_client_packet_t*)bData;
	CStringA strProtocol(packet->protocl, sizeof(packet->protocl));
	strProtocol.Trim();
	int iDataLength = nDataLen - sizeof(packet->protocl) - sizeof(packet->detailType) - s_strProtoDelimiter.GetLength();
	if (iDataLength > 0)
	{
		CStringA strData(packet->data, iDataLength);
		if (strProtocol.CompareNoCase("HYLB") == 0)
		{
			m_tickCount = 0;
			//更新合约列表
			InitHeYueLieBiao(strData);			
		//合约列表更新之后,更新一下品种的有效交易时间
		InitTradeTime();
	}
	else if (strProtocol.CompareNoCase("NHYLB") == 0)
	{
		CWriteLock writelock(g_lockNHeyueLiebiao);
		g_strNHeyueLiebiao = strData;
		// Update NHYLB contract list expiration date
		UpdateNHYLBExpireDate(g_strNHeyueLiebiao);
		// Save contract list to local file
		FILE* file = fopen("./ncontractJson.txt", "w+");
		if (file != NULL)
		{
			fwrite(g_strNHeyueLiebiao.GetString(), 1, g_strNHeyueLiebiao.GetLength(), file);
			fclose(file);
		}
	}
	else if (strProtocol.CompareNoCase("OPHYXX") == 0)
	{
		InitOPHYXXAndPect(strData,true);
	}
		//收到新的行情之后要先去重,保存到原始行情队列,然后将原始的行情解析成前端需要的行情格式,并计算内外盘并更新最新行情map表,
		//然后生成全量数据字符串添加到200条全量数据队列,同时计算增量字符串,添加到5分钟缓存队列.并把增量字符串根据订阅列表推送给客户端
	else if (strProtocol.CompareNoCase("HQOD") == 0 || strProtocol.CompareNoCase("HQDY") == 0)
		{
			if (!isTimeBetween4pmAnd8pm()) {
				g_queueHQDYData.enqueue(strData);
				//统计数量
				++m_tickCount;
				if (m_tickCount % 10000 == 0)
					g_Logger->info("行情累计数量:{}", m_tickCount);
			}
		}
	}
}
//发送心跳包
BOOL CTcpPullClientImpl::SendHeartBeat()
{
	//发送心跳包
	CBufferPtr resultBuffer;
	CStringA strRes;
	strRes.Format("HBET    TXT %s", (LPCSTR)s_strProtoDelimiter);
	resultBuffer.Cat((const BYTE*)(LPCSTR)strRes, strRes.GetLength());
	if (m_Client->Send(resultBuffer.Ptr(), resultBuffer.Size()))
		return TRUE;
	else
	{
		CString strLog;
		strLog.Format(_T("到行情转发服务的心跳包发送失败: %d|%s"), ::SYS_GetLastError(), ::HP_GetSocketErrorDesc(SE_DATA_SEND));
		g_Logger->info(strLog);
		return FALSE;
	}
}
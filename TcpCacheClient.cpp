#include "TcpCacheClient.h"
#include <iostream>
#include "WorkerProcessor.h"

#define SAFEDELETE(pObject) if( pObject != NULL ) {delete pObject; pObject = NULL;}

extern CTcpServer s_server;
extern bool g_bRun;
//与缓存服务器通信时,自己生成的客户端id
atomic<UINT32> CTcpCacheClient::s_iCacheReqId(0);
//最近一次连接成功的时间
time_t CTcpCacheClient::s_tLastSuc = 0;
//最近一次连接失败的时间
time_t CTcpCacheClient::s_tLastFail = 0;
//最近一次断线的时间
time_t CTcpCacheClient::s_tLastClose = 0;
//当天已发送断线的总条数
int CTcpCacheClient::s_iDXTotalNum = 0;

CTcpCacheClient::CTcpCacheClient()
	: m_pRecvBuffer(NULL), m_Client(this), m_tActiveTime(0)	
{
}

CTcpCacheClient::~CTcpCacheClient()
{	
	Stop();
	//通知数据处理线程停止工作并退出
	m_bRun = false;
}

CStringA CTcpCacheClient::GetIPByDomain(const char* szHostName)
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

//连接到缓存服务器
BOOL CTcpCacheClient::StartClient(const CStringA& strServerHostName, short usPort)
{
	CString strLog;
	USES_CONVERSION;
	SetClientState(ST_STARTING);
	time_t tNow = ::time(NULL);
	if (m_Client->Start(A2T(strServerHostName), usPort, FALSE))
	{
		SetClientState(ST_STARTED);				
		g_Logger->info(_T("已连接到缓存服务器{}:{}"), (LPCSTR)strServerHostName, usPort);
		//初始化心跳活跃时间
		m_tActiveTime = tNow;
		//发送连接成功的短信通知
		if (g_bSendSms && tNow - s_tLastSuc >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
		{
			++s_iDXTotalNum;
			SendSms("连接缓存服务器成功!");
			s_tLastSuc = tNow;
		}
		return TRUE;
		
	}
	else
	{
		//发送连接失败的短信通知
		if (g_bSendSms && tNow - s_tLastFail >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
		{
			++s_iDXTotalNum;
			SendSms("连接缓存服务器失败!");
			s_tLastFail = tNow;
		}
		SetClientState(ST_STOPPED);
		strLog.Format(_T("连接到缓存服务器失败: %d|%s"), m_Client->GetLastError(), m_Client->GetLastErrorDesc());
		g_Logger->info(strLog);		
		return FALSE;
	}
}

void CTcpCacheClient::Stop()
{
	if (m_enClientState == ST_STARTED || ST_STARTING == m_enClientState)
	{
		SetClientState(ST_STOPPING);
		VERIFY(m_Client->Stop());
		SetClientState(ST_STOPPED);
		g_Logger->info("缓存客户端已停止!");
	}
}

void CTcpCacheClient::SetClientState(EnAppState state)
{
	m_enClientState = state;
	if (ST_STOPPED == state)
	{
		//将连接活跃时间设置为0.以便尽快触发断线重连
		m_tActiveTime = 0;
	}
}

EnHandleResult CTcpCacheClient::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
	return HR_OK;
}

EnHandleResult CTcpCacheClient::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	return HR_OK;
}

void CALLBACK HandleCacheDataProc(TSocketTask* pTask)
{
	CTcpCacheClient* pThis = (CTcpCacheClient*)pTask->sender;
	if (pThis)
	{
		pThis->Parse(pTask->buf, pTask->bufLen, pTask->connID);
	}
}
EnHandleResult CTcpCacheClient::OnReceive(ITcpClient* pSender, CONNID dwConnID, int iLength)
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
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleCacheDataProc, this, dwConnID, pData2, nTotalLen);
					if (!g_pHPThreadPool->Submit(pTask))
					{
						::HP_Destroy_SocketTaskObj(pTask);
						g_Logger->error(_T("%s->向线程池提交任务失败!"), __FUNCTION__);
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
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleCacheDataProc, this, dwConnID, pData2, nTotalLen);
					if (!g_pHPThreadPool->Submit(pTask))
					{
						::HP_Destroy_SocketTaskObj(pTask);
						g_Logger->error(_T("%s->向线程池提交任务失败!"), __FUNCTION__);
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

EnHandleResult CTcpCacheClient::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
	CString strLog;
	strLog.Format(_T("%s-->与缓存服务器的连接关闭."), _T(__FUNCTION__));
	g_Logger->info(strLog);
	SetClientState(ST_STOPPED);
	//发送连接关闭的短信通知
	time_t tNow = ::time(NULL);
	if (g_bSendSms && tNow - s_tLastClose >= g_iDXInterval && s_iDXTotalNum < g_iDXTotalNum)
	{
		++s_iDXTotalNum;
		SendSms("与缓存服务器的连接断开!");
		s_tLastClose = tNow;
	}
	return HR_OK;
}

//解析数据包
void CTcpCacheClient::Parse(const BYTE* bData, int nDataLen, CONNID dwConnID)
{	
	//只要有数据就更新最近活跃时间
	m_tActiveTime = ::time(NULL);

	cache_packet_t* packet = (cache_packet_t*)bData;
	UINT32 iDataSize = ntohl(packet->size);
	switch (packet->cmd)
	{
	//缓存数据应答
	case cmd_KLINE_CACHE_RSP:
	{
		OPKLineCacheResponse cacheRsp;
		if (cacheRsp.ParseFromArray(packet->data, iDataSize))
		{			
			UINT32 iReqId = cacheRsp.irequestid();
			bool bFind = false;
			ClientKLineCache clientCache;
			//将缓存服务器的数据与本地的数据拼接起来
			g_lockClientKLineCache.lock();
			auto itMap = g_mapClientKLineCache.find(iReqId);
			if (itMap != g_mapClientKLineCache.end())
			{
				bFind = true;
				clientCache = itMap->second;
				g_mapClientKLineCache.erase(itMap);
			}
			g_lockClientKLineCache.unlock();
			if (bFind)
			{
				//auto t1 = std::chrono::steady_clock::now();
				//g_Logger->info("reqid:{} 从发送缓存到接受缓存应答并反序列化时间:{}微秒", clientCache.iClientReqId, std::chrono::duration<double, std::micro>(t1 - clientCache.tCacheReq).count());
				cacheRsp.set_irequestid(clientCache.iClientReqId);
				if (clientCache.bNeedNative)
				{
					list<KLine> listKline;
					//从本地数据补充
					switch (clientCache.klineType)
					{
					case KLineType::KM1:
					{
						g_lock1MinKline.lock();
						auto it1m = g_map1MinKlineDay.find(clientCache.optionId);
						if (it1m != g_map1MinKlineDay.end())
						{
							it1m->second.GetTimeKLines(clientCache.iEndTimeStamp, listKline);
						}
						g_lock1MinKline.unlock();
						break;
					}
					case KLineType::KM5:
					{
						g_lock5MinKline.lock();
						auto it5m = g_map5MinKline.find(clientCache.optionId);
						if (it5m != g_map5MinKline.end())
						{
							it5m->second.GetTimeKLines(clientCache.iEndTimeStamp, listKline);
						}
						g_lock5MinKline.unlock();
						break;
					}
					case KLineType::KDAY:
					{
						g_lockDayKline.lock();
						auto itDay = g_mapDayKLine.find(clientCache.optionId);
						if (itDay != g_mapDayKLine.end() && itDay->second.IsValid())
						{
							listKline.push_front(itDay->second);
						}
						g_lockDayKline.unlock();
						break;
					}
					default:
						break;
					}
					//将本地的数据补充到KLineCacheResponse里面
					for (const KLine& kline : listKline)
					{
						pbOPKLine* pKline = cacheRsp.add_arrklines();
						pKline->set_itradedate(atoi(kline.strTradeDate.c_str()));
						pKline->set_itimestamp(kline.iTimeStamp);
						pKline->set_dopen(kline.dOpen);
						pKline->set_dhigh(kline.dHigh);
						pKline->set_dlow(kline.dLow);
						pKline->set_dclose(kline.dClose);
						pKline->set_i64volume(kline.i64Volume);
						pKline->set_i64positionqty(kline.i64PositionQty);
					}
					//auto t2 = std::chrono::steady_clock::now();
					//g_Logger->info("reqid:{} 补充当天数据时间:{}微秒", clientCache.iClientReqId, std::chrono::duration<double, std::micro>(t2 - t1).count());
				}
				string strOutput;
				cacheRsp.SerializeToString(&strOutput);
				string strResData;
				if (clientCache.protocolType == ProtocolType::CCACHE)
					strResData.append("OPCCACHE", 8);
				else if (clientCache.protocolType == ProtocolType::TCACHE)
					strResData.append("OPTCACHE", 8);
				strResData.append("PBUF", 4);
				strResData.append(strOutput);
				strResData.append(s_strProtoDelimiter);
				//加包头
				front_packet_t res_pkt;
				res_pkt.cmd = cmd_NO_NEED_HANDLE;
				res_pkt.reqid = clientCache.iFrontEndReqId;
				res_pkt.size = htonl(strResData.size());
				string resultBuffer;
				resultBuffer.append(string((LPCSTR)&res_pkt, sizeof(res_pkt)));
				resultBuffer.append(strResData);
				if(!s_server.Send(clientCache.dwConnId, (LPCBYTE)resultBuffer.c_str(), resultBuffer.size()))
					g_Logger->error("{}->K线缓存数据应答发送失败,链接id为:{} 客户端请求id为:{} ", __FUNCTION__, clientCache.dwConnId, clientCache.iClientReqId);
				//记录时间消耗
				//auto t3 = std::chrono::steady_clock::now();
				//g_Logger->info("connid:{} reqid:{} 从收到缓存请求到给客户端发送应答时间:{}微秒", clientCache.dwConnId, clientCache.iClientReqId, std::chrono::duration<double, std::micro>(t3 - clientCache.tCacheReq).count());
			}
		}
		break;
	}
	//心跳成功
	case cmd_KLINESERVER_HEARTBEAT_SUC:
	{
		break;
	}
	default:
		break;
	}
}
//发送心跳包
BOOL CTcpCacheClient::SendHeartBeat()
{
	//发送心跳包
	CBufferPtr resultBuffer;
	cache_packet_t ok_pack;
	ok_pack.cmd = cmd_KLINESERVER_HEARTBEAT;
	ok_pack.size = 0;
	resultBuffer.Cat((const BYTE*)&ok_pack, sizeof(cache_packet_t));
	if (m_Client->Send(resultBuffer.Ptr(), resultBuffer.Size()))
	{
		return TRUE;
	}
	else
	{
		g_Logger->error("到缓存服务器的心跳包发送失败: {}|{}", ::SYS_GetLastError(), ::HP_GetSocketErrorDesc(SE_DATA_SEND));
		return FALSE;
	}
}
//向缓存服务器发送获取指定根数K线的请求
BOOL CTcpCacheClient::SendKLineCountReq(const OptionID& optionid, UINT32 iEndTimeStamp, UINT32 iCount, KLineType klineType, UINT32& iReqId)
{
	OPKLineCountRequest klineCountReq;
	iReqId = s_iCacheReqId.fetch_add(1, std::memory_order::memory_order_seq_cst);
	klineCountReq.set_irequestid(iReqId);
	auto pOptionid = klineCountReq.mutable_optionid();
	pOptionid->set_strexchange(optionid.strExchage);
	pOptionid->set_strcomodityxyz(optionid.strComodity);
	pOptionid->set_strcontractno(optionid.strContractNo);
	pOptionid->set_strcallorputflag(string(1, optionid.cCallOrPutFlag));
	pOptionid->set_strstrikeprice(optionid.strStrikePrice);
	klineCountReq.set_iendtimestamp(iEndTimeStamp);
	klineCountReq.set_icount(iCount);
	switch (klineType)
	{
	case KLineType::KM1:
		klineCountReq.set_type(pbOPKLineType::M1);
		break;
	case KLineType::KM5:
		klineCountReq.set_type(pbOPKLineType::M5);
		break;
	case KLineType::KDAY:
		klineCountReq.set_type(pbOPKLineType::DAY);
		break;
	default:
		break;
	}
	//序列化到string对象
	string strOutput;
	klineCountReq.SerializeToString(&strOutput);

	cache_packet_t res_pack;
	res_pack.cmd = cmd_KLINE_COUNT_REQ;
	res_pack.size = htonl(strOutput.size());
	string resultBuffer;
	resultBuffer = string((LPCSTR)&res_pack, sizeof(res_pack));
	resultBuffer.append(strOutput);

	if (m_Client->Send((LPCBYTE)resultBuffer.c_str(), resultBuffer.size()))
	{
		//g_Logger->info("{}->给定根数K线缓存的请求发送成功,请求id为:{},QuoteID:{}-{}-{},时间根数:{}-{}", __FUNCTION__, iReqId, quoteid.strExchage.c_str(), quoteid.strComodity.c_str(), quoteid.strContractNo.c_str(), iEndTimeStamp, iCount);
		return TRUE;
	}
	else
	{
		g_Logger->error("{}->给定根数K线缓存的请求发送失败: {}|{}", __FUNCTION__, ::SYS_GetLastError(), ::HP_GetSocketErrorDesc(SE_DATA_SEND));
		return FALSE;
	}
}
//向缓存服务器发送获取指定起始时间K线的请求
BOOL CTcpCacheClient::SendKLineTimeReq(const OptionID& optionid, UINT32 iStartTimeStamp, UINT32 iEndTimeStamp, KLineType klineType, UINT32& iReqId)
{
	OPKLineTimeRequest klineTimeReq;
	iReqId = s_iCacheReqId.fetch_add(1, std::memory_order::memory_order_seq_cst);
	klineTimeReq.set_irequestid(iReqId);
	auto pOptionid = klineTimeReq.mutable_optionid();
	pOptionid->set_strexchange(optionid.strExchage);
	pOptionid->set_strcomodityxyz(optionid.strComodity);
	pOptionid->set_strcontractno(optionid.strContractNo);
	pOptionid->set_strcallorputflag(string(1, optionid.cCallOrPutFlag));
	pOptionid->set_strstrikeprice(optionid.strStrikePrice);
	klineTimeReq.set_istarttimestamp(iStartTimeStamp);
	klineTimeReq.set_iendtimestamp(iEndTimeStamp);
	switch (klineType)
	{
	case KLineType::KM1:
		klineTimeReq.set_type(pbOPKLineType::M1);
		break;
	case KLineType::KM5:
		klineTimeReq.set_type(pbOPKLineType::M5);
		break;
	case KLineType::KDAY:
		klineTimeReq.set_type(pbOPKLineType::DAY);
		break;
	default:
		break;
	}
	//序列化到string对象
	string strOutput;
	klineTimeReq.SerializeToString(&strOutput);

	cache_packet_t res_pack;
	res_pack.cmd = cmd_KLINE_TIME_REQ;
	res_pack.size = htonl(strOutput.size());
	string resultBuffer;
	resultBuffer = string((LPCSTR)&res_pack, sizeof(res_pack));
	resultBuffer.append(strOutput);

	if (m_Client->Send((LPCBYTE)resultBuffer.c_str(), resultBuffer.size()))
	{
		//g_Logger->info("{}->按照起止时间获取K线缓存的请求发送成功,请求id为:{},QuoteID:{}-{}-{},起止时间:{}-{}", __FUNCTION__, iReqId, quoteid.strExchage.c_str(), quoteid.strComodity.c_str(), quoteid.strContractNo.c_str(), iStartTimeStamp, iEndTimeStamp);
		return TRUE;
	}
	else
	{
		g_Logger->error("{}->按照起止时间获取K线缓存的请求发送失败: {}|{}", __FUNCTION__, ::SYS_GetLastError(), ::HP_GetSocketErrorDesc(SE_DATA_SEND));
		return FALSE;
	}
}
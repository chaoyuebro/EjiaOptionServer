#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include "Util.h"
#include "WorkerProcessor.h"
#include "timer/PQTimer.h"
#include <iostream>
#include "AliOSSUtil.h"
#include "TcpPullClientImpl.h"
#include "TcpCacheClient.h"
#include "client/linux/handler/exception_handler.h"
//程序崩溃时回调的BreakPad函数
static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
	void* context,
	bool succeeded)
{
	std::cout << "Dump path:" << descriptor.path() << std::endl;
	return succeeded;
}

#define SAFEDELETE(pObject) if( pObject != NULL ) {delete pObject; pObject = NULL;}
void DetachConnInfo(ITcpServer* pSender, CONNID dwConnID)
{
	CBufferPtr* pRecvBuffer = NULL;
	BOOL bExist = pSender->GetConnectionExtra(dwConnID, (PVOID*)&pRecvBuffer);
	if (bExist && NULL != pRecvBuffer)
	{
		SAFEDELETE(pRecvBuffer);
		pSender->SetConnectionExtra(dwConnID, (PVOID)NULL);
		//pSender->Disconnect(dwConnID);
	}
}
//提交给线程池的回调函数. 任务处理完成后,pTask对象由线程池自动销毁
void CALLBACK HandleWorkerData(TSocketTask* pTask)
{
	ITcpServer* pTcpServer = (ITcpServer*)pTask->sender;
	//解析数据包并返回客户端数据
	string resultBuffer;
	bool bForceDisconnect = false;
	WorkerProcessor::Parse(pTask->buf, pTask->bufLen, pTask->connID, resultBuffer, bForceDisconnect);
	if (resultBuffer.size() > 0)
	{
		pTcpServer->Send(pTask->connID, (LPCBYTE)resultBuffer.c_str(), resultBuffer.size());
	}
	//服务端主动关闭与打码端的连接
	if (bForceDisconnect)
		pTcpServer->Disconnect(pTask->connID);
}
class CListenerImpl : public CTcpServerListener
{
public:
	virtual EnHandleResult OnPrepareListen(ITcpServer* pSender, SOCKET soListen) override
	{
		TCHAR szAddress[50];
		int iAddressLen = sizeof(szAddress) / sizeof(TCHAR);
		USHORT usPort;

		pSender->GetListenAddress(szAddress, iAddressLen, usPort);
		::PostOnPrepareListen(szAddress, usPort);

		return HR_OK;
	}

	virtual EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient) override
	{
		return HR_OK;
		//BOOL bPass = TRUE;
		//TCHAR szAddress[50];
		//int iAddressLen = sizeof(szAddress) / sizeof(TCHAR);
		//USHORT usPort;

		//pSender->GetRemoteAddress(dwConnID, szAddress, iAddressLen, usPort);

		//if (!g_app_arg.reject_addr.IsEmpty())
		//{
		//	if (g_app_arg.reject_addr.CompareNoCase(szAddress) == 0)
		//		bPass = FALSE;
		//}

		////::PostOnAccept(dwConnID, szAddress, usPort, bPass);

		//return bPass ? HR_OK : HR_ERROR;
	}

	virtual EnHandleResult OnHandShake(ITcpServer* pSender, CONNID dwConnID) override
	{
		return HR_OK;
	}

	virtual EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override
	{
		CBufferPtr* pRecvBuffer = NULL;
		if (!pSender->GetConnectionExtra(dwConnID, (PVOID*)&pRecvBuffer))
			return HR_IGNORE;
		EnHandleResult rs = HR_ERROR;
		BYTE* pData2 = (BYTE*)pData;
		int iLength2 = iLength;

		if (NULL == pRecvBuffer)//新连接
		{
			//将所有完整的包数据添加进数据队列
			while (true)
			{
				int nTotalLen = WorkerProcessor::get_length(pData2, iLength2);
				if (nTotalLen > 0)//收到一个完整包
				{
					//将该完整包提交给线程池处理
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleWorkerData, pSender, dwConnID, pData2, nTotalLen);
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
					pRecvBuffer = new CBufferPtr(pData2, iLength2);
					pSender->SetConnectionExtra(dwConnID, (PVOID)pRecvBuffer);
					break;
				}
			}
			rs = HR_OK;
		}
		else//已有连接,并且缓冲区里面已经有数据
		{
			pRecvBuffer->Cat(pData2, iLength2);
			//将所有完整的包数据添加进数据队列
			pData2 = pRecvBuffer->Ptr();
			iLength2 = pRecvBuffer->Size();
			while (true)
			{
				int nTotalLen = WorkerProcessor::get_length(pData2, iLength2);
				if (nTotalLen > 0)//收到一个完整包
				{
					//将该完整包提交给线程池处理
					LPTSocketTask pTask = ::HP_Create_SocketTaskObj(HandleWorkerData, pSender, dwConnID, pData2, nTotalLen);
					if (!g_pHPThreadPool->Submit(pTask))
					{
						::HP_Destroy_SocketTaskObj(pTask);
						g_Logger->error(_T("%s->>向线程池提交任务失败!"), __FUNCTION__);
					}
					//继续检测是否有完整包
					pData2 += nTotalLen;
					iLength2 -= nTotalLen;
				}
				else//数据包不完整，判断是否需要截断数据并保存
				{
					if (iLength2 != pRecvBuffer->Size())
					{
						//将处理完成的数据剔除掉
						CBufferPtr* ptr = new CBufferPtr(pData2, iLength2);
						//删除之前的缓冲区对象
						SAFEDELETE(pRecvBuffer);
						pSender->SetConnectionExtra(dwConnID, (PVOID)ptr);
					}
					break;
				}
			}
			rs = HR_OK;
		}

		return rs;

		return HR_ERROR;
	}

	virtual EnHandleResult OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength) override
	{
		//::PostOnSend(dwConnID, pData, iLength);
		return HR_OK;
	}

	virtual EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) override
	{
		//从需要推送数据的连接集合里面删除该连接
		{
			CCriSecLock locallock(g_lockFrontEndConnids);
			g_setFrontEndConnids.erase(dwConnID);
		}
		//删除连接上面的附加数据
		DetachConnInfo(pSender, dwConnID);
		return HR_OK;
	}

	virtual EnHandleResult OnShutdown(ITcpServer* pSender) override
	{
		::PostOnShutdown();
		return HR_OK;
	}

};

CListenerImpl s_listener;
CTcpServer s_server(&s_listener);
//需要同时连接两台行情转发服务,防止其中一台宕机.需要对它们接收到的行情做去重
CTcpPullClientImpl* g_pClient1 = nullptr;
CTcpPullClientImpl* g_pClient2 = nullptr;
CTcpCacheClient* g_pCacheClient = nullptr;
//定时器对象
PQTimer g_pqTimer;
void timer_proc(PQTimer* pPQTimer)
{
	while (true)
	{
		pPQTimer->Update();
		//休眠100毫秒
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}
void TimerSendHeartBeat()
{
	if (g_pClient1)
	{
		if (g_pClient1->m_enClientState == ST_STARTED)
			g_pClient1->SendHeartBeat();
		//断线重连
		else if (g_pClient1->m_enClientState == ST_STOPPED)
			g_pClient1->StartClient(g_mapProfile["serverIP1"], atoi(g_mapProfile["serverPort1"]));
	}
	if (g_pClient2)
	{
		if (g_pClient2->m_enClientState == ST_STARTED)
			g_pClient2->SendHeartBeat();
		//断线重连
		else if (g_pClient2->m_enClientState == ST_STOPPED)
			g_pClient2->StartClient(g_mapProfile["serverIP2"], atoi(g_mapProfile["serverPort2"]));
	}
	if (g_pCacheClient)
	{
		if (g_pCacheClient->m_enClientState == ST_STARTED)
			g_pCacheClient->SendHeartBeat();
		//断线重连
		else if (g_pCacheClient->m_enClientState == ST_STOPPED)
			g_pCacheClient->StartClient(g_mapProfile["cacheServerIP"], atoi(g_mapProfile["cacheServerPort"]));
	}
	g_pqTimer.Schedule(30 * 100, TimerSendHeartBeat);
}
//每隔5分钟把tick增量数据写入文件并压缩放入缓存
void TimerCompressTick()
{
	Save5MinTick();
	SaveKLine();
	ClearCache();
	g_pqTimer.Schedule(5 * 60 * 100, TimerCompressTick);
}
//每隔5分钟判断一次，如果已经收盘，就上传数据到阿里云
void TimerUploadData()
{
	//重新读取一次配置文件里面的ismain字段.因为如果主服务器挂掉,可以直接修改从服务器的配置文件,修改完不用重启就能生效
	ParseProfile(true);
	UploadDataToAliOSS();
	g_pqTimer.Schedule(5 * 60 * 100, TimerUploadData);
}
//每隔一分钟，更新一次TradeDayList和TradeServerList
void TimerUpdateTradeDayList()
{
	InitTradeDayList();
	g_pqTimer.Schedule(60 * 100, TimerUpdateTradeDayList);
}
void OnCmdStart(CCommandParser* pParser)
{
	if (s_server.Start(g_app_arg.bind_addr, atoi(g_mapProfile["port"])))
	{
		//初始化当前的交易日.将初始化放到这里,是因为启动的时候,会一直等待获取合约列表和初始化缓存。如果转发服务到20点30分以后才返回合约列表,就会导致当前交易日初始化错误,
		//而且由于定时器线程没有启动,ClearCache没有被调用, g_iCurTradeDate也没有被更新。第二天服务器没有保存增量tick数据。客户端也获取不到DRMX数据
		g_iCurTradeDate = GetTradeDateInt();

		::LogServerStart(g_app_arg.bind_addr, atoi(g_mapProfile["port"]));
		//给定时器添加任务
		g_pqTimer.Schedule(30 * 100, TimerSendHeartBeat);
		g_pqTimer.Schedule(5 * 60 * 100, TimerCompressTick);
		g_pqTimer.Schedule(5 * 60 * 100, TimerUploadData);
		g_pqTimer.Schedule(60 * 100, TimerUpdateTradeDayList);
		std::thread hqdy_thread(HandleHQDYDateProc, nullptr);
		std::thread quote_thread(HandledQuoteDataProc, nullptr);

		//开启定时器线程
		std::thread thread(timer_proc, &g_pqTimer);
		//等待线程运行结束,实际上就是为了将主线程挂起,程序不会退出
		thread.join();
	}
	else
		::LogServerStartFail(s_server.GetLastError(), s_server.GetLastErrorDesc());
}
int main()
{
	//程序如果崩溃,BreakPad库会生成dump文件在/tmp目录下
	google_breakpad::MinidumpDescriptor descriptor("/tmp");
	google_breakpad::ExceptionHandler eh(descriptor, nullptr, dumpCallback, nullptr, true, -1);
	//初始化日志对象
	if (access("./logs/", F_OK) != 0)
	{
		if (mkdir("./logs/", 0777) == -1)
		{
			std::cerr << "创建logs目录失败" << std::endl;
			return EXIT_CODE_OK;
		}
	}
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] %v");
	g_Logger = spdlog::daily_logger_mt("asyncFileLogger", "./logs/Daily.log", 23, 59);
	g_Logger->set_level(level::level_enum::info);
	g_Logger->flush_on(level::level_enum::info);
	g_Logger->info("{}->程序启动", __FUNCTION__);
	//初始化阿里云OSS SDK
	AliOSSUtil::InitSDK();
	//解析配置文件
	ParseProfile();
	//设置阿里云OSS的内网或外网地址
	auto itOssEndPoint = g_mapProfile.find("ossEndPoint");
	if(itOssEndPoint != g_mapProfile.end() && !itOssEndPoint->second.IsEmpty())
		AliOSSUtil::s_Endpoint = g_mapProfile["ossEndPoint"];
	for (auto itProfile = g_mapProfile.begin(); itProfile != g_mapProfile.end(); ++itProfile)
		g_Logger->info("{}={}", itProfile->first.c_str(), itProfile->second.c_str());
	//记录下服务器的启动时间
	g_tServerStartTime = ::time(NULL);

	CTermAttrInitializer term_attr;
	CAppSignalHandler s_signal_handler({ SIGTTOU, SIGINT });
	s_server.SetKeepAliveTime(g_app_arg.keep_alive ? TCP_KEEPALIVE_TIME : 0);
	//创建线程池对象
	if (g_pHPThreadPool == nullptr)
	{
		g_pHPThreadPool = ::HP_Create_ThreadPool();
		if (g_pHPThreadPool == nullptr)
		{
			g_Logger->error("线程池对象创建失败!");
			return EXIT_CODE_OK;
		}
		//启动线程池
		if (!g_pHPThreadPool->Start())
		{
			g_Logger->error("线程池启动失败");
			return EXIT_CODE_OK;
		}
		else
			g_Logger->error("线程池启动成功");
	}
	InitOPHYXX();
	InitTradeDayList();
	//启动的时候获取一下精度和有效交易时间
	InitTradeTime();
	//启动客户端连接到行情缓存服务
	if (g_pCacheClient == nullptr)
		g_pCacheClient = new CTcpCacheClient();
	if (!g_pCacheClient->StartClient(g_mapProfile["cacheServerIP"], atoi(g_mapProfile["cacheServerPort"])))
		g_Logger->error("{}->连接到缓存服务器失败!", __FUNCTION__);
	//启动客户端连接到行情转发服务
	if (g_pClient1 == nullptr)
		g_pClient1 = new CTcpPullClientImpl("FL@G4AES8LJF3R3RT5POE9KFA@HFASF$;NASF(*&J;JFEASFJKJ;FEAF^FEAFHLL");
	if (g_pClient2 == nullptr)
		g_pClient2 = new CTcpPullClientImpl("FL@G4AES8LJF3R3RT5POE9KFA@HFASF$;NASF(*&J;JFEASFJKJ;FEAF^FEAFHLL");
	if (!g_pClient1->StartClient(g_mapProfile["serverIP1"], atoi(g_mapProfile["serverPort1"])) || !g_pClient2->StartClient(g_mapProfile["serverIP2"], atoi(g_mapProfile["serverPort2"])))
	{
		g_Logger->error("{}->连接到行情转发服务失败!", __FUNCTION__);
	}
	//创建处理行情转发客户端数据队列的线程
	std::thread zhuanfa_clientThread(CTcpPullClientImpl::HandleTCPDateProc, nullptr);
	zhuanfa_clientThread.detach();
	//等待缓存初始化完成
	std::unique_lock<std::mutex> mlock(g_lockInitCache);
	g_cvInitCache.wait(mlock);
	//直接启动
	OnCmdStart(nullptr);

	AliOSSUtil::UninitSDK();
	HP_Destroy_ThreadPool(g_pHPThreadPool);
	g_pHPThreadPool = nullptr;
	//删除所有的spd logger对象
	spdlog::drop_all();
	return EXIT_CODE_OK;
}
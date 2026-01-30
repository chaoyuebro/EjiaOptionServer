#include "Util.h"
#include "AliOSSUtil.h"
#include "libcurl/include/curl.h"
#include "MD5.h"
#include "TcpPullClientImpl.h"
#include <dirent.h>
#include "WorkerProcessor.h"
#include "CountDownLatch.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <chrono>
#include <thread>
#include <atomic>
#ifdef __GLIBC__
#include <malloc.h>  // for malloc_trim
#endif
extern CTcpServer s_server;

//条件变量,用于通知主线程 缓存初始化已经完成, 主线程开始启动服务器、监听端口并启动定时器线程等后续操作
std::mutex g_lockInitCache;
std::condition_variable g_cvInitCache;
//文本协议分隔符
CStringA s_strProtoDelimiter = "@%&)";
//本地数据保存的根目录
const string g_strLocalDataDir = "/usr/ejia7";
//是否是上传数据的服务器
bool g_bStorageServer = false;
//是否是主服务器
volatile bool g_bIsMain = false;
//是否发短信
bool g_bSendSms = false;
//发短信的最短间隔
int g_iDXInterval;
//每天发短信的总条数
int g_iDXTotalNum;
//当前交易日. 为了防止DRMX数据里面出现不是当前交易日的数据
atomic_int g_iCurTradeDate(0);
//记录服务器启动时间, 为了防止收盘后向阿里云重复上传文件
time_t g_tServerStartTime = 0;
//前置网关的连接id 需要推送行情
set<CONNID> g_setFrontEndConnids;
CCriSec g_lockFrontEndConnids;
//行情转发数据队列
BlockingQueue<string> g_queueHQDYData;
//处理过的行情数据队列
BlockingQueue<shared_ptr<HandledQuoteData>> g_queueHandledData;
//客户端等待缓存服务器的应答数据
map<UINT32, ClientKLineCache> g_mapClientKLineCache;
CCriSec g_lockClientKLineCache;
//当天1分钟线的时间与根数的映射,用于判断缓存数据是否需要去缓存服务器获取,降低对g_map1MinKlineDay的加锁频率
map<OptionID, KLineCount> g_map1MinKlineCount;
CCriSec g_lock1MinKlineCount;
//当天5分钟线的时间与根数的映射,用于判断缓存数据是否需要去缓存服务器获取,降低对g_map5MinKline的加锁频率
map<OptionID, KLineCount> g_map5MinKlineCount;
CCriSec g_lock5MinKlineCount;

//保存已经上传过合约列表的交易日
set<CString> g_setUploadTradeDate;
//保存每个品种的有效交易时间,key是交易所+品种编码比如rb,value是多个时间区间.如果交易时间是夜盘跨天,比如21:00-2:00,则会把区间分割成两个21:00-24:00和0:00-2:00
map<ComodityID, list<STimeSegment>> g_mapProductTradeTime;
CSimpleRWLock g_lockProductTradeTime;
//合约列表
CStringA g_strHeyueLiebiao;
CStringA g_strNHeyueLiebiao;
CStringA g_strOPHeyueLiebiao;
CStringA g_strMSHeyueLiebiao;
//全部品种id下面包含的期权id列表
map<ContractID, list<OptionID>> g_mapOptionIDs;
CSimpleRWLock g_lockHeyueLiebiao;
CSimpleRWLock g_lockNHeyueLiebiao;
CSimpleRWLock g_lockOPHeyueLiebiao;
CSimpleRWLock g_lockMSHeyueLiebiao;
//线程池对象
IHPThreadPool* g_pHPThreadPool = NULL;
//原始行情列表,每5分钟往文件写一次,并清空
map<OptionID, string> g_mapNativeQuotes;
//已经上传过原始数据的QuoteID,防止重复上传
set<OptionID> g_setUploadNativeQuoteId;
CCriSec g_lockNativeQuotes;
//行情去重队列,长度是g_iUinqueLen,默认为5000.用list + set来实现. list主要用来实现长度控制和先进先出,set主要用来实现去重和提高查找速度
const int g_iUinqueLen = 5000;
set<CString> g_setUniqueTick;
list<CString> g_listUniqueTick;
CCriSec g_lockUniqueTick;
//保存每一个合约的最新的计算过内外盘和均价的行情
map<OptionID, HandledQuoteData> g_mapLastQuote;
CCriSec g_lockLastQuote;
//map<OptionID, std::pair<double, double>> g_mapLastSetPrice;
//CCriSec g_lockLastSetPrice;
//保存每个品种的指数的计算精度,key是交易所+品种编码比如rb,value是指数的计算精度,意思是保留的小数点的位数,0就是没有小数位,1就是保留一位小数
map<ComodityID, ComodityInfo> g_mapComodityInfo;
//期权合约信息
CStringA g_strOPHYXX;
CSimpleRWLock g_lockComodityInfo;
//TradeDay.LIST. 现在改由行情服务器获取
string g_strTradeDayList;
CCriSec g_lockTradeDayList;

//近两百条全量数据
map<OptionID, list<CString>> g_map200QuanTick;
CCriSec g_lock200QuanTick;
//最近5分钟的增量数据
map<OptionID, string> g_map5MinTick;
CCriSec g_lock5MinTick;
//当天全部未压缩的tick增量数据,不包括当前5分钟的
map<OptionID, string> g_mapUnCompressTick;
//当天全部已压缩的tick增量数据,不包括当前5分钟的. value不是c风格的字符串,而是字节集
map<OptionID, string> g_mapCompressTick;
//已经上传过tick数据的QuoteID,防止重复上传
set<OptionID> g_setUploadTickQuoteId;
CCriSec g_lockCompressTick;
//当天1分钟线
map<OptionID, KLineCache> g_map1MinKlineDay;
//已经上传过1M数据的QuoteID,防止重复上传
set<OptionID> g_setUpload1MQuoteId;
CCriSec g_lock1MinKline;
//当天5分钟线
map<OptionID, KLineCache> g_map5MinKline;
//已经上传过5M数据的QuoteID,防止重复上传
set<OptionID> g_setUpload5MQuoteId;
CCriSec g_lock5MinKline;
//日线
map<OptionID, KLine> g_mapDayKLine;
//已经上传过日线数据的QuoteID,防止重复上传
set<OptionID> g_setUploadDayQuoteId;
CCriSec g_lockDayKline;
atomic_long lastUpdateHYXXtime;
//读取config.properties配置文件
map<CString, CString> g_mapProfile;
//是否恢复过缓存
volatile bool g_bRecoverCache = false;
//bOnlyIsMain参数如果为true,则只解析到IsMain配置项并且只更新g_bIsMain字段。
void ParseProfile(bool bOnlyIsMain)
{
	const char* path = "./config.properties";

	FILE *file = fopen(path, "r");
	if (file == NULL)
	{
		g_Logger->error("{}->config.properties文件打开失败!", __FUNCTION__);
		return;
	}

	while (true)
	{
		char buf[1024] = { 0 };

		if (fgets(buf, 1023, file) == NULL) break;
		//开头的字符是#的行是注释,直接跳过
		if (buf[0] == '#')
			continue;
		char left[100] = { 0 }, right[100] = { 0 };
		bool bRight = false;
		char* curPos = left;
		for (int i = 0; i < strlen(buf); i++)
		{
			if (buf[i] == '=' && !bRight)
			{
				curPos = right;
				bRight = true;
				continue;
			}

			if (buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n')
			{
				*curPos = buf[i];
				curPos++;
			}
		}
		CString strLeft(left), strRight(right);
		if (bOnlyIsMain && strLeft.Trim().CompareNoCase("ismain") == 0)
		{
			if (strRight.Trim().CompareNoCase("true") == 0)
				g_bIsMain = true;
			break;
		}
		else
		{
			g_mapProfile[strLeft.Trim()] = strRight.Trim();
		}
	}
	fclose(file);
	if (!bOnlyIsMain)
	{
		if (g_mapProfile["storage"].Trim().CompareNoCase("true") == 0)
			g_bStorageServer = true;
		if (g_mapProfile["ismain"].Trim().CompareNoCase("true") == 0)
			g_bIsMain = true;
		if (g_mapProfile["sendsms"].Trim().CompareNoCase("true") == 0)
			g_bSendSms = true;
		g_iDXInterval = atoi(g_mapProfile["DXInterval"].Trim());
		g_iDXTotalNum = atoi(g_mapProfile["DXTotalNum"].Trim());
	}
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
	string *str = (string *)userp;
	str->append((char *)ptr, size*nmemb);
	return size * nmemb;
}
//用libcurl发起http GET请求
int HttpCommonGet(string sUrl, string &sResv)
{
	string result;
	CURLcode res;
	CURL* curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, sUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
		res = curl_easy_perform(curl);
		if (CURLE_OK == res) {
			sResv = result;
		}
		curl_easy_cleanup(curl);
	}
	return res;
}
//根据品种查询出精度
int QryJingDu(const CString& strExchange, const CString& strComodity)
{
	CString comodity(strComodity);
	int index = strComodity.Find("&");
	//如果是套利品种,则用其中一个品种的精度来计算
	if (index != -1)
		comodity = strComodity.Left(index);
	CReadLock readlock(g_lockComodityInfo);
	auto it = g_mapComodityInfo.find(ComodityID(strExchange, comodity));
	if (it != g_mapComodityInfo.end())
		return it->second.iPect;
	else
	{
		g_Logger->error("{}->找不到品种{}-{}的计算精度!", __FUNCTION__, (LPCSTR)strExchange, (LPCSTR)strComodity);
		time_t nowTime = time(nullptr);
		if (nowTime - lastUpdateHYXXtime > 300)
		{
			ifstream hyxxIfs;
			hyxxIfs.open("./OPHYXXJson.txt");
			if (hyxxIfs)
			{
				string strHYXXJson = string((std::istreambuf_iterator<char>(hyxxIfs)), std::istreambuf_iterator<char>());
				InitOPHYXXAndPect(strHYXXJson, false);
			}
			lastUpdateHYXXtime = nowTime;
		}
	}
	return 0;
}
//查询品种的tick
double QryTick(const CString& strExchange, const CString& strComodity)
{
	CString comodity(strComodity);
	int index = strComodity.Find("&");
	//如果是套利品种,则用其中一个品种的精度来计算
	if (index != -1)
		comodity = strComodity.Left(index);
	CReadLock readlock(g_lockComodityInfo);
	auto it = g_mapComodityInfo.find(ComodityID(strExchange, comodity));
	if (it != g_mapComodityInfo.end())
		return it->second.dTick;
	else
		g_Logger->error("{}->找不到品种{}-{}的最小变动价tick!", __FUNCTION__, (LPCSTR)strExchange, (LPCSTR)strComodity);
	return 0.0;
}
//将价格按照最小变动价tick进行对齐. 四舍五入(dPrice/dTick) * dTick
double PriceAlignTick(double dPrice, double dTick)
{
	double dRes = dPrice;
	if (dTick > 0.0)
	{
		dRes = round(dPrice / dTick) * dTick;
	}
	return dRes;
}
//从后台页面获取品种的有效交易时间信息
bool GetTradeTime(string& strRes)
{
	bool bSucess = false;
	for (int i = 0; i < 3; ++i)
	{
		if (CURLE_OK == HttpCommonGet((LPCSTR)g_mapProfile["breadTimeSegment"], strRes))
		{
			bSucess = true;
			break;
		}
	}
	if (bSucess)
		g_Logger->info("{}->获取合约有效交易时间成功", __FUNCTION__);
	else
		g_Logger->info("{}->获取合约有效交易时间失败", __FUNCTION__);
	return bSucess;
}
//解析json内容并g_mapProductTradeTime结构
void InitTradeTime()
{
	static volatile time_t tInitTime = 0;
	time_t tnow = ::time(NULL);
	if (tnow - tInitTime < 60)
		return;
	tInitTime = tnow;
	CStringA strJson;
	if (GetTradeTime(strJson))
	{
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseDefaultFlags>(strJson);
		if (doc.HasParseError())
		{
			g_Logger->error("{}->parse json error:{}", __FUNCTION__, (LPCSTR)strJson);
			return;
		}
		auto &data = doc["data"];
		if (!data.IsArray())
		{
			g_Logger->error("{}->data is not array!", __FUNCTION__);
			return;
		}
		for (size_t i = 0; i < data.Size(); ++i)
		{
			auto &breedlist = data[i]["breedlist"];
			if (!breedlist.IsArray())
			{
				g_Logger->error("{}->breedlist is not array!", __FUNCTION__);
				return;
			}
			for (size_t j = 0; j < breedlist.Size(); j++)
			{
				auto &product = breedlist[j];
				CString strProductId = product["code"].GetString();
				CString strExchange = product["ecode"].GetString();
				auto &TimeSegment = product["TimeSegment"];
				if (!TimeSegment.IsArray())
				{
					g_Logger->error("{}->TimeSegment of {} is not array!", __FUNCTION__, strProductId);
					continue;
				}
				//更新合约交易时间
				CWriteLock2 writelock_time(g_lockProductTradeTime);
				auto &listTimes = g_mapProductTradeTime[ComodityID(strExchange, strProductId)];
				listTimes.clear();
				for (size_t k = 0; k < TimeSegment.Size(); k++)
				{
					auto &times = TimeSegment[k];
					std::string flag2 = times["flag2"].GetString();
					std::string flag1 = times["flag1"].GetString();
					//判断时间是否跨天
					if (flag2 == flag1)
					{
						listTimes.push_back(STimeSegment(atoi(times["from"].GetString()), atoi(times["to"].GetString())));
					}
					else
					{
						//跨天的话, 就把时间区间分割成两个
						listTimes.push_back(STimeSegment(atoi(times["from"].GetString()), 240000));
						listTimes.push_back(STimeSegment(0, atoi(times["to"].GetString())));
					}
				}
				writelock_time.unlock();
				CString strLog;
				for (auto timeSegment : listTimes)
					strLog.AppendFormat("[%d,%d] ", timeSegment.From, timeSegment.To);
				g_Logger->info("{}-{} {}", (LPCSTR)strExchange, (LPCSTR)strProductId, (LPCSTR)strLog);
			}
		}
		g_Logger->info("{} complete.", __FUNCTION__);
	}

}
//从阿里云oss上面获取Option/Contract/TradeDay.LIST文件并初始化服务器上面的缓存副本
void InitTradeDayList()
{
	string strCompress;
	if (!AliOSSUtil::DownloadFile(g_mapProfile["dig"] + "/Contract/TradeDay.LIST", strCompress))
	{
		g_Logger->critical("{}->从OSS获取Contract/TradeDay.LIST文件失败!", __FUNCTION__);
		return;
	}
	g_lockTradeDayList.lock();
	g_strTradeDayList = strCompress;
	g_lockTradeDayList.unlock();
}

//解析json内容并初始化g_mapComodityInfo结构和期权合约信息
void InitOPHYXXAndPect(const CStringA& strJson, const bool& isUpdate)
{
	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseDefaultFlags>(strJson);
	if (doc.HasParseError())
	{
		g_Logger->error("{}->parse json error:{}", __FUNCTION__, (LPCSTR)strJson);
		return;
	}
	auto &exchange = doc["exchange"];
	if (!exchange.IsArray())
	{
		g_Logger->error("{}->exchange is not array!", __FUNCTION__);
		return;
	}
	for (size_t i = 0; i < exchange.Size(); ++i)
	{
		CString strExchange = exchange[i]["code"].GetString();
		auto &cmdt = exchange[i]["cmdt"];
		if (!cmdt.IsArray())
		{
			g_Logger->error("{}->cmdt is not array!", __FUNCTION__);
			return;
		}
		for (size_t j = 0; j < cmdt.Size(); j++)
		{
			CString strComodity = cmdt[j]["code"].GetString();
			//更新品种的精度和最小变动价
			CWriteLock2 writelock_pect(g_lockComodityInfo);
			g_mapComodityInfo[ComodityID(strExchange, strComodity)] = ComodityInfo(cmdt[j]["pect"].GetInt(), cmdt[j]["tick"].GetDouble());
			writelock_pect.unlock();
		}
	}
	CWriteLock2 writelock_hyxx(g_lockComodityInfo);
	//更新合约信息
	if (!strJson.empty() && isUpdate)
	{
		g_strOPHYXX = strJson;
		FILE* file = fopen("./OPHYXXJson.txt", "w+");
		if (file)
		{
			fwrite(strJson.data(), 1, strJson.length(), file);
			fclose(file);
		}
	}
}

void InitOPHYXX()
{
	time_t nowTime = time(nullptr);
	if (nowTime - lastUpdateHYXXtime > 300)
	{
		ifstream hyxxIfs;
		hyxxIfs.open("./OPHYXXJson.txt");
		if (hyxxIfs)
		{
			string strHYXXJson = string((std::istreambuf_iterator<char>(hyxxIfs)), std::istreambuf_iterator<char>());
			InitOPHYXXAndPect(strHYXXJson, false);
		}
		lastUpdateHYXXtime = nowTime;
	}
}

//发送短信
void SendSms(const CString& strContent, const CString& strMobile)
{
	//短信平台的url、用户名和密码
	string strSmsUrl = "http://api.djsms.cn:8088/msgHttp/json/mt";
	string strAccount = "jiaoyi106";
	string strPwd = "ejia72wsx@WSX";
	//用libcurl构造http post请求
	time_t curTime = ::time(NULL);
	curTime *= 1000;
	CString strPwdPlain;
	strPwdPlain.Format("%s%s%lld", strPwd.c_str(), strMobile.c_str(), curTime);

	CString strPostData;
	MD5 md5;
	strPostData.Format("account=%s&password=%s&mobile=%s&timestamps=%lld&content=", strAccount.c_str(), md5.ToMD5(strPwdPlain, "").c_str(), strMobile.c_str(), curTime);
	strPostData += strContent;
	CURLcode res;
	CURL* curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, strSmsUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strPostData.c_str());
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
		res = curl_easy_perform(curl);
		if (CURLE_OK != res) {
			g_Logger->error("{}->请求短信平台失败,错误代码:{}", __FUNCTION__, res);
		}
		curl_easy_cleanup(curl);
	}
}
//发送短信
void SendSms(const CString& strMsg)
{
	string strMobiles = g_mapProfile["mobiles"];
	if (!strMobiles.empty())
	{
		CString strContent = CString("【郑州期米信息】") + g_mapProfile["name"] + strMsg;
		//中文是gb2312编码,把编码转成UTF-8
		int iDestLength = strContent.size() * 2;
		char* szDest = new char[iDestLength];
		memset(szDest, 0, iDestLength);
		SYS_GbkToUtf8(strContent.c_str(), szDest, iDestLength);
		strContent = szDest;
		delete[] szDest;
		//对strContent进行urlencode
		DWORD dwDestLen = SYS_GuessUrlEncodeBound((LPCBYTE)strContent.c_str(), strContent.size());
		BYTE* lpszDest = new BYTE[dwDestLen + 1];
		memset(lpszDest, 0, dwDestLen + 1);
		int iRet = SYS_UrlEncode((LPBYTE)strContent.c_str(), strContent.size(), lpszDest, dwDestLen);
		CString strContentEncode = (LPCSTR)lpszDest;
		delete[] lpszDest;
		vector<CString> vecMobileData;
		SplitStr(strMobiles.c_str(), vecMobileData, ",");
		for (CString strMobile : vecMobileData)
			SendSms(strContentEncode, strMobile.Trim());
	}
}
//更新合约列表并解析ContractJson,生成全部品种id列表
void InitHeYueLieBiao(const CStringA& strData)
{
	static volatile time_t tInitTime = 0;
	time_t tnow = ::time(NULL);
	if (tnow - tInitTime < 60)
		return;
	tInitTime = tnow;
	{
		CWriteLock writelock(g_lockHeyueLiebiao);
		g_mapOptionIDs.clear();
		//解析json结构
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseDefaultFlags>(strData);
		if (doc.HasParseError())
		{
			g_Logger->error("{}->parse json error:{}", __FUNCTION__, (LPCSTR)strData);
			return;
		}
		auto &exchange = doc["exchange"];
		if (exchange.IsNull() || !exchange.IsArray())
		{
			g_Logger->error("{}->exchange 解析错误:{}!", __FUNCTION__, (LPCSTR)strData);
			return;
		}
		for (size_t i = 0; i < exchange.Size(); ++i)
		{
			CString strExchange = exchange[i]["code"].GetString();
			auto& cmdt = exchange[i]["cmdt"];
			if (!cmdt.IsArray())
			{
				g_Logger->error("{}->{} cmdt is not array:{}", __FUNCTION__, (LPCSTR)strExchange, (LPCSTR)strData);
				return;
			}
			for (size_t j = 0; j < cmdt.Size(); j++)
			{
				CString strComodity = cmdt[j]["code"].GetString();
				auto& contract = cmdt[j]["contract"];
				if (!contract.IsArray())
				{
					g_Logger->error("{}->{}-{} contract is not array:{}", __FUNCTION__, (LPCSTR)strExchange, (LPCSTR)strComodity, (LPCSTR)strData);
					return;
				}
				for (size_t k = 0; k < contract.Size(); ++k)
				{
					CString strContractNo = contract[k]["code"].GetString();
					ContractID contractID(strExchange.MakeUpper(), strComodity.MakeUpper(), strContractNo);
					auto& strike = contract[k]["strike"];
					if (!strike.IsArray())
					{
						g_Logger->error("{}->{}-{}-{} strike is not array:{}", __FUNCTION__, (LPCSTR)strExchange, (LPCSTR)strComodity, (LPCSTR)strContractNo, (LPCSTR)strData);
						return;
					}
					list<OptionID>& listOptionIDs = g_mapOptionIDs[contractID];
					listOptionIDs.clear();
					for (size_t m = 0; m < strike.Size(); ++m)
					{
						CString strStrikePrice = strike[m].GetString();
						listOptionIDs.push_back(OptionID((LPCSTR)contractID.strExchage, (LPCSTR)contractID.strComodity, (LPCSTR)contractID.strContractNo, 'C', (LPCSTR)strStrikePrice));
						listOptionIDs.push_back(OptionID((LPCSTR)contractID.strExchage, (LPCSTR)contractID.strComodity, (LPCSTR)contractID.strContractNo, 'P', (LPCSTR)strStrikePrice));
					}
				}
			}
		}
		//更新合约列表字符串
		g_strHeyueLiebiao = strData;
		//合约列表里面添加期权到期日
		UpdateHYLBExpireDate(g_strHeyueLiebiao);
		//打印出g_mapOptionIDs的内容
		for (auto itQuoteId = g_mapOptionIDs.begin(); itQuoteId != g_mapOptionIDs.end(); ++itQuoteId)
		{
			CString strLog;
			strLog.Format("%s-%s-%s: ", (LPCSTR)itQuoteId->first.strExchage, (LPCSTR)itQuoteId->first.strComodity, (LPCSTR)itQuoteId->first.strContractNo);
			for (const OptionID& optionid : itQuoteId->second)
				strLog.AppendFormat("%s,", (LPCSTR)optionid.strStrikePrice);
			g_Logger->info(strLog.c_str());
		}
	}
	g_Logger->info("{} complete.", __FUNCTION__);
	//缓存恢复
	if (!g_bRecoverCache)
	{
		g_bRecoverCache = true;
		int iMonth;
		if (!IsInClearCacheTime(iMonth))
			RecoverCache();
		else
			//如果在清理当天缓存的时间段内.则只恢复当月1分钟的缓存
			RecoverCache(true);
	}
}
//判断新到来的行情是否是最新的,如果是则需要推送给客户端并计算K线
bool IsNewTick(const CString& strTick)
{
	bool bRes = false;
	g_lockUniqueTick.lock();
	auto itSet = g_setUniqueTick.find(strTick);
	if (itSet == g_setUniqueTick.end())
	{
		bRes = true;
		g_setUniqueTick.insert(strTick);
		g_listUniqueTick.push_back(strTick);
		if (g_listUniqueTick.size() >= g_iUinqueLen)
		{
			CString strOld = g_listUniqueTick.front();
			g_listUniqueTick.pop_front();
			g_setUniqueTick.erase(strOld);
		}
	}
	g_lockUniqueTick.unlock();
	return bRes;
}
//将收到的原始的行情解析成前端需要的行情格式
bool ParseQuote(const OptionQuoteRsp& quoteRsp, ExchangeQutoData& quoteData)
{
	auto baseQuoteRsp = quoteRsp.basequote();
	//交易日
	quoteData.strTradeDate = baseQuoteRsp.strtradedate();
	//交易所
	quoteData.strExchage = baseQuoteRsp.strexchange();
	quoteData.strComodity = baseQuoteRsp.strcomodity();
	//合约
	quoteData.strContractNo = baseQuoteRsp.strcontractno();
	//涨跌标识和行权价格
	quoteData.cCallOrPutFlag = quoteRsp.strcallorputflag()[0];
	quoteData.strStrikePrice = quoteRsp.strstrikeprice();
	//时间
	quoteData.strDateTime = baseQuoteRsp.strdatetimestamp();
	//计算时间戳
	struct tm tmTime;
	memset(&tmTime, 0, sizeof(struct tm));
	//时间24时制
	strptime(quoteData.strDateTime, "%Y-%m-%d %H:%M:%S", &tmTime);
	quoteData.tTimeStamp = mktime(&tmTime);
	//毫秒
	quoteData.strMilisec = baseQuoteRsp.strmilisec();
	//昨收盘
	quoteData.QPreClosingPrice = baseQuoteRsp.dqpreclosingprice();
	quoteData.QPreSettlePrice = baseQuoteRsp.dqpresettleprice();
	quoteData.QSettlePrice = baseQuoteRsp.qsettleprice();
	/*OptionID optionId(quoteData.strExchage, quoteData.strComodity, quoteData.strContractNo, quoteData.cCallOrPutFlag, quoteData.strStrikePrice);
	if (quoteData.QPreSettlePrice < 0.0001)
	{
		quoteData.QPreSettlePrice = g_mapLastSetPrice[optionId].first;
	}
	else {
		g_lockLastSetPrice.lock();
		g_mapLastSetPrice[optionId].first = quoteData.QPreSettlePrice;
		g_lockLastSetPrice.unlock();
		quoteData.QSettlePrice = quoteData.QPreSettlePrice;
		if (g_mapLastSetPrice[optionId].second > 0.0001) {
			quoteData.QPreSettlePrice = g_mapLastSetPrice[optionId].second;
		}
	}*/
	quoteData.QPrePositionQty = baseQuoteRsp.i64qprepositionqty();
	quoteData.QOpeningPrice = baseQuoteRsp.dqopeningprice();
	quoteData.QLastPrice = baseQuoteRsp.dqlastprice();
	quoteData.QHighPrice = baseQuoteRsp.dqhighprice();
	quoteData.QLowPrice = baseQuoteRsp.dqlowprice();
	quoteData.QLimitUpPrice = baseQuoteRsp.dqlimitupprice();
	quoteData.QLimitDownPrice = baseQuoteRsp.dqlimitdownprice();
	quoteData.QTotalQty = baseQuoteRsp.i64qtotalqty();
	quoteData.QPositionQty = baseQuoteRsp.i64qpositionqty();
	quoteData.QClosingPrice = baseQuoteRsp.dqclosingprice();
	quoteData.QLastQty = baseQuoteRsp.i64qlastqty();
	quoteData.QBidPrice[0] = baseQuoteRsp.dqbidprice1();
	quoteData.QBidPrice[1] = baseQuoteRsp.dqbidprice2();
	quoteData.QBidPrice[2] = baseQuoteRsp.dqbidprice3();
	quoteData.QBidPrice[3] = baseQuoteRsp.dqbidprice4();
	quoteData.QBidPrice[4] = baseQuoteRsp.dqbidprice5();
	quoteData.QBidQty[0] = baseQuoteRsp.i64qbidqty1();
	quoteData.QBidQty[1] = baseQuoteRsp.i64qbidqty2();
	quoteData.QBidQty[2] = baseQuoteRsp.i64qbidqty3();
	quoteData.QBidQty[3] = baseQuoteRsp.i64qbidqty4();
	quoteData.QBidQty[4] = baseQuoteRsp.i64qbidqty5();
	quoteData.QAskPrice[0] = baseQuoteRsp.dqaskprice1();
	quoteData.QAskPrice[1] = baseQuoteRsp.dqaskprice2();
	quoteData.QAskPrice[2] = baseQuoteRsp.dqaskprice3();
	quoteData.QAskPrice[3] = baseQuoteRsp.dqaskprice4();
	quoteData.QAskPrice[4] = baseQuoteRsp.dqaskprice5();
	quoteData.QAskQty[0] = baseQuoteRsp.i64qaskqty1();
	quoteData.QAskQty[1] = baseQuoteRsp.i64qaskqty2();
	quoteData.QAskQty[2] = baseQuoteRsp.i64qaskqty3();
	quoteData.QAskQty[3] = baseQuoteRsp.i64qaskqty4();
	quoteData.QAskQty[4] = baseQuoteRsp.i64qaskqty5();
	return true;
}
//判断给定的tick是否在有效交易时间
bool IsInTradeTime(ExchangeQutoData& quoteData)
{
	//不是当前交易日的数据,不计算K线
	if (atoi(quoteData.strTradeDate.c_str()) != g_iCurTradeDate)
		return false;
	bool bRes = false;
	CString strComodity = quoteData.strComodity;
	if (quoteData.tTimeStamp != 0)
	{
		struct tm tms = { 0 };
		//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
		localtime_r(&quoteData.tTimeStamp, &tms);
		//int iHour, iMin, iSec;
		//iHour = iMin = iSec = 0;
		//sscanf((LPCSTR)quoteData.strDateTime + 11,"%d:%d:%d", &iHour, &iMin, &iSec);
		int iTime = tms.tm_hour * 10000 + tms.tm_min * 100 + tms.tm_sec;

		CReadLock readlock(g_lockProductTradeTime);
		auto itMap = g_mapProductTradeTime.find(ComodityID(quoteData.strExchage, strComodity));
		if (itMap != g_mapProductTradeTime.end())
		{
			for (STimeSegment& timeSegment : itMap->second)
			{
				//左闭右开
				if (timeSegment.From <= iTime && iTime < timeSegment.To)
				{
					bRes = true;
					break;
				}
				//如果时间在收盘时间往后的10秒以内,则把行情里面的时间戳改成收盘前的最后一秒,这样在计算K线的时候能让它落到最后一根K线里面
				else if (timeSegment.To <= iTime && iTime < (timeSegment.To + 10))
				{
					quoteData.tTimeStamp -= (iTime - timeSegment.To + 1);
					bRes = true;
					break;
				}
			}
		}
		else
		{
			g_Logger->error("{}->找不到品种{}-{}的有效交易时间", __FUNCTION__, (LPCSTR)quoteData.strExchage, (LPCSTR)strComodity);
		}
	}
	else
	{
		g_Logger->error("{}->行情时间解析错误:{}-{}-{}-{}-{}-{}-{}-{}", __FUNCTION__, (LPCSTR)quoteData.strTradeDate, (LPCSTR)quoteData.strExchage, (LPCSTR)quoteData.strComodity, (LPCSTR)quoteData.strContractNo, quoteData.cCallOrPutFlag, (LPCSTR)quoteData.strStrikePrice, (LPCSTR)quoteData.strDateTime, quoteData.tTimeStamp);
	}
	return bRes;
}
//判断是否是正确的数据
bool IsCorrectData(const HandledQuoteData& handledData, const HandledQuoteData* pCacheData)
{
	bool bRes = false;
	if (handledData.QTotalQty <= 0)
		return bRes;
	if (handledData.QLastPrice <= 0.0)
		return bRes;
	if (pCacheData == nullptr || handledData.QTotalQty != pCacheData->QTotalQty)
		bRes = true;
	return bRes;
}
//计算均价
void CalcAvgPrice(HandledQuoteData& handledData, const HandledQuoteData* pCacheData)
{
	if (!IsCorrectData(handledData, pCacheData))
	{
		if (pCacheData != nullptr)
			handledData.dAvgPrice = pCacheData->dAvgPrice;
		else
			handledData.dAvgPrice = 0.0;
	}
	else
	{
		if (pCacheData == nullptr)
		{
			handledData.dAvgPrice = handledData.QLastPrice;
		}
		else
		{
			double oValue = pCacheData->dAvgPrice * pCacheData->QTotalQty;
			double nValue = handledData.QLastPrice * (handledData.QTotalQty - pCacheData->QTotalQty);
			handledData.dAvgPrice = (oValue + nValue) / handledData.QTotalQty;
		}
	}
}
//计算内外盘成交量
void CalcInOutValue(HandledQuoteData& handledData, const HandledQuoteData* pCacheData)
{
	if (!IsCorrectData(handledData, pCacheData))
	{
		if (pCacheData != nullptr)
		{
			handledData.iNeiPan = pCacheData->iNeiPan;
			handledData.iWaiPan = pCacheData->iWaiPan;
		}
		else
		{
			handledData.iNeiPan = 0;
			handledData.iWaiPan = 0;
		}
	}
	else
	{
		//内盘数量
		int inValue = 0;
		//外盘数量
		int outValue = 0;
		//平均值
		double aveValue = 0.0;
		//新增成交量
		int nQty = 0;
		if (pCacheData == nullptr)
		{
			aveValue = handledData.QLastPrice;
			nQty = handledData.QTotalQty;
		}
		else
		{
			inValue = pCacheData->iNeiPan;
			outValue = pCacheData->iWaiPan;
			int currentQty = handledData.QTotalQty;
			int lastQty = pCacheData->QTotalQty;
			nQty = currentQty - lastQty;
			double bidPrice0 = pCacheData->QBidPrice[0];
			double askPrice0 = pCacheData->QAskPrice[0];
			if (bidPrice0 > 0.0 && askPrice0 > 0.0)
				aveValue = (bidPrice0 + askPrice0) / 2;
			else
				aveValue = handledData.QLastPrice;
		}
		//新增成交量为负数,说明处理tick数据时又乱序了,程序报错
		if (nQty < 0)
			g_Logger->error("{}->新增成交量为负数,当前tick:{},最近一条tick:{}", __FUNCTION__, handledData.ToQuanTick().c_str(), pCacheData->ToQuanTick().c_str());
		if (handledData.QLastPrice > aveValue)
		{
			outValue = outValue + nQty;
		}
		else if (handledData.QLastPrice < aveValue)
		{
			inValue = inValue + nQty;
		}
		else
		{
			outValue = outValue + nQty / 2;
			inValue = inValue + nQty / 2;
			if (nQty % 2 == 1)
			{
				outValue = outValue + 1;
			}
		}
		handledData.iNeiPan = inValue;
		handledData.iWaiPan = outValue;
	}
}
//处理新的行情,计算内外盘和均价
bool HandleQuote(HandledQuoteData& handledData, CString& strTick, bool bHQOld, bool& bVolume)
{
	OptionID optionId(handledData.strExchage, handledData.strComodity, handledData.strContractNo, handledData.cCallOrPutFlag, handledData.strStrikePrice);
	//根据上一条行情来计算内外盘和均价
	{
		CCriSecLock locallockLastQuote(g_lockLastQuote);
		auto itMap = g_mapLastQuote.find(optionId);
		//必须得有cache数据  &&  cache数据的交易日与最新数据的交易日一致
		if (itMap != g_mapLastQuote.end() && handledData.strTradeDate.CompareNoCase(itMap->second.strTradeDate) == 0)
		{
			//如果新Quote的"时间戳" || "当日总成交量"小于LastQuote, 则直接返回false, 表示 直接丢弃该Quote, 不做后续的行情推送和K线计算
			if (handledData.tTimeStamp < itMap->second.tTimeStamp || handledData.QTotalQty < itMap->second.QTotalQty)
				return false;
			CalcAvgPrice(handledData, &itMap->second);
			CalcInOutValue(handledData, &itMap->second);
			if (handledData.tTimeStamp - itMap->second.tTimeStamp > 60)
				//生成全量字符串
				strTick = handledData.ToQuanTick();
			else
				//生成增量字符串
				strTick = handledData.ToZengTick(itMap->second);
			if (itMap->second.QTotalQty != handledData.QTotalQty)
				bVolume = true;
		}
		else
		{
			bVolume = true;
			CalcAvgPrice(handledData, nullptr);
			CalcInOutValue(handledData, nullptr);
			//生成全量字符串
			strTick = handledData.ToQuanTick();
		}
		//更新最新的计算过的行情
		g_mapLastQuote[optionId] = handledData;
	}
	if (!bHQOld)
	{
		//如果不是当前交易日的数据,都丢弃掉。防止客户端获取DRMX数据时,还获取到不是当前交易日的数据
		if (atoi(handledData.strTradeDate.c_str()) != g_iCurTradeDate)
			return true;
		//更新200条全量数据的队列
		CString strQuanTick = handledData.ToQuanTick();
		g_lock200QuanTick.lock();
		auto it200 = g_map200QuanTick.find(optionId);
		if (it200 != g_map200QuanTick.end())
		{
			it200->second.push_back(std::move(strQuanTick));
			if (it200->second.size() > 200)
				it200->second.pop_front();
		}
		else
		{
			list<CString>& listQuantick = g_map200QuanTick[optionId];
			listQuantick.push_back(std::move(strQuanTick));
		}
		g_lock200QuanTick.unlock();
		//更新5分钟增量数据队列
		g_lock5MinTick.lock();
		auto it5Min = g_map5MinTick.find(optionId);
		if (it5Min != g_map5MinTick.end())
		{
			it5Min->second += (LPCSTR)strTick;
		}
		else
		{
			string& str5Min = g_map5MinTick[optionId];
			str5Min += (LPCSTR)strTick;
		}
		g_lock5MinTick.unlock();
	}
	return true;
}
//GZIP压缩数据
int CompressData(const string& strOriginalData, string& strCompress)
{
	DWORD dwDestLen = SYS_GuessCompressBound(strOriginalData.size(), true);
	BYTE* lpszDest = new BYTE[dwDestLen];
	memset(lpszDest, 0, sizeof(BYTE) * dwDestLen);
	int iRet = SYS_GZipCompress((const BYTE*)strOriginalData.c_str(), strOriginalData.size(), lpszDest, dwDestLen);
	if (0 == iRet)
		strCompress = std::move(string((LPCSTR)lpszDest, dwDestLen));
	delete[] lpszDest;
	return iRet;
}
//GZIP解压缩数据
int UnCompressData(const string& strCompress, string& strUnCompress)
{
	int iRet = -1;
	DWORD dwDestLen = SYS_GZipGuessUncompressBound((LPCBYTE)strCompress.c_str(), strCompress.size());
	if (dwDestLen == 0)
		return iRet;
	BYTE* lpszDest = new BYTE[dwDestLen];
	memset(lpszDest, 0, sizeof(BYTE) * dwDestLen);
	iRet = SYS_GZipUncompress((LPCBYTE)strCompress.c_str(), strCompress.size(), lpszDest, dwDestLen);
	if (0 == iRet)
		strUnCompress = std::move(string((LPCSTR)lpszDest, dwDestLen));
	delete[] lpszDest;
	return iRet;
}
//从阿里云oss下载并解压文件
bool GetAndUnComFromOSS(const string& ossFileName, string& strUnCompress)
{
	if (!AliOSSUtil::IsFileExist(ossFileName))
	{
		g_Logger->info("{}->OSS上{}文件不存在!", __FUNCTION__, ossFileName.c_str());
		return false;
	}
	string strCompress;
	if (!AliOSSUtil::DownloadFile(ossFileName, strCompress))
	{
		g_Logger->critical("{}->从OSS获取{}文件失败!", __FUNCTION__, ossFileName.c_str());
		return false;
	}
	int iRet = UnCompressData(strCompress, strUnCompress);
	if (-1 == iRet)
	{
		g_Logger->error("{}->推测 Gzip 解压结果长度失败! {}文件并非有效的 Gzip 格式.", __FUNCTION__, ossFileName.c_str());
		return false;
	}
	else if (0 == iRet)
	{
		return true;
	}
	else
	{
		g_Logger->error("{}->{}文件解压出错,错误代码:{}", __FUNCTION__, ossFileName.c_str(), iRet);
		return false;
	}
}
void CompressTick(const OptionID& quoteId, const string& strTick)
{
	string strCompress;
	int iRet = CompressData(strTick, strCompress);
	if (0 == iRet)
		g_mapCompressTick[quoteId] = strCompress;
	else
		g_Logger->error("{}->{}-{}-{}压缩出错,错误代码:{}", __FUNCTION__, quoteId.strExchage, quoteId.strComodity, quoteId.strContractNo, iRet);

}
//每隔5分钟重新压缩一次当天tick数据并把当前5分钟的tick数据写入文件
void Save5MinTick()
{
	map<OptionID, string> mapTemp;
	g_lock5MinTick.lock();
	mapTemp = g_map5MinTick;
	g_map5MinTick.clear();
	g_lock5MinTick.unlock();
	//开始压缩
	g_lockCompressTick.lock();
	for (auto it5Min = mapTemp.begin(); it5Min != mapTemp.end(); ++it5Min)
	{
		if (it5Min->second.size() > 0)
		{
			auto itUnCompress = g_mapUnCompressTick.find(it5Min->first);
			if (itUnCompress != g_mapUnCompressTick.end())
			{
				itUnCompress->second += it5Min->second;
				CompressTick(it5Min->first, itUnCompress->second);
			}
			else
			{
				string& strTick = g_mapUnCompressTick[it5Min->first];
				//如果配置文件里面storage=false 需要先把 交易所|品种(处理过的添加了XYZ)|合约\n  添加到压缩包的前面,方便返回给客户端
				if (!g_bStorageServer)
					strTick = it5Min->first.ToString();
				strTick += it5Min->second;
				CompressTick(it5Min->first, strTick);
			}
		}
	}
	g_lockCompressTick.unlock();
	//把近5分钟的增量tick数据写入文件
	for (auto itTemp = mapTemp.begin(); itTemp != mapTemp.end(); ++itTemp)
	{
		if (itTemp->second.size() > 0)
			CQuoteWriter::WriteQuote(itTemp->first.strExchage, itTemp->first.strComodity, itTemp->first.strContractNo, itTemp->first.cCallOrPutFlag, itTemp->first.strStrikePrice, itTemp->second, GetTradeDate(), false);
	}
	//把近5分钟的原始tick数据写入文件
	mapTemp.clear();
	g_lockNativeQuotes.lock();
	mapTemp = g_mapNativeQuotes;
	g_mapNativeQuotes.clear();
	g_lockNativeQuotes.unlock();
	for (auto itTemp = mapTemp.begin(); itTemp != mapTemp.end(); ++itTemp)
	{
		if (itTemp->second.size() > 0)
			CQuoteWriter::WriteQuote(itTemp->first.strExchage, itTemp->first.strComodity, itTemp->first.strContractNo, itTemp->first.cCallOrPutFlag, itTemp->first.strStrikePrice, itTemp->second, GetTradeDate(), true);
	}
}
//每隔5分钟保存一下1m,5m和日线数据
void SaveKLine()
{
	//保存1m线
	map<OptionID, KLineCache> map1mTemp, map5mTemp;
	map<OptionID, KLine> mapdayTemp;
	g_lock1MinKline.lock();
	map1mTemp = g_map1MinKlineDay;
	g_lock1MinKline.unlock();
	string strCurTradeDate = GetTradeDate();
	for (auto itMap = map1mTemp.begin(); itMap != map1mTemp.end(); ++itMap)
	{
		string strTradeDate = itMap->second.lastKLine.strTradeDate;
		if (strTradeDate.empty())
			strTradeDate = strCurTradeDate;
		CQuoteWriter::Write1M(itMap->first.strExchage, itMap->first.strComodity, itMap->first.strContractNo, itMap->first.cCallOrPutFlag, itMap->first.strStrikePrice, itMap->second.ToString(), strTradeDate);
	}
	//保存5m线
	g_lock5MinKline.lock();
	map5mTemp = g_map5MinKline;
	g_lock5MinKline.unlock();
	for (auto itMap = map5mTemp.begin(); itMap != map5mTemp.end(); ++itMap)
	{
		string strTradeDate = itMap->second.lastKLine.strTradeDate;
		if (strTradeDate.empty())
			strTradeDate = strCurTradeDate;
		CQuoteWriter::Write5M(itMap->first.strExchage, itMap->first.strComodity, itMap->first.strContractNo, itMap->first.cCallOrPutFlag, itMap->first.strStrikePrice, itMap->second.ToString(), strTradeDate);
	}
	//保存日线
	g_lockDayKline.lock();
	mapdayTemp = g_mapDayKLine;
	g_lockDayKline.unlock();
	for (auto itMap = mapdayTemp.begin(); itMap != mapdayTemp.end(); ++itMap)
	{
		string strTradeDate = itMap->second.strTradeDate;
		if (strTradeDate.empty())
			strTradeDate = strCurTradeDate;
		CQuoteWriter::WriteDay(itMap->first.strExchage, itMap->first.strComodity, itMap->first.strContractNo, itMap->first.cCallOrPutFlag, itMap->first.strStrikePrice, (LPCSTR)itMap->second.ToDayString(), strTradeDate);
	}
}
//获取给定时间所在的交易日,返回格式为yyyyMMdd
int GetTradeDateInt(time_t tTimeStamp, bool bClearCache)
{
	time_t now, timep;
	now = timep = tTimeStamp;
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&now, &tms);
	//0是周日
	int weekDay = tms.tm_wday;
	int iHourMin = tms.tm_hour * 10000 + tms.tm_min * 100 + tms.tm_sec;

	int iHourDelimiter = 200000;
	if (bClearCache)
		iHourDelimiter = 210000;

	//周六加两天
	if (weekDay == 6)
	{
		timep += 2 * 24 * 60 * 60;
	}
	// 周五夜盘加三天
	else if (weekDay == 5 && iHourMin >= iHourDelimiter)
	{
		timep += 3 * 24 * 60 * 60;
	}
	//周日 或者 其它夜盘 加一天
	else if (weekDay == 0 || iHourMin >= iHourDelimiter)
	{
		timep += 24 * 60 * 60;
	}
	localtime_r(&timep, &tms);
	int iYmd = (tms.tm_year + 1900) * 10000 + (tms.tm_mon + 1) * 100 + tms.tm_mday;
	return iYmd;
}
//得到当前交易日,返回格式为yyyyMMdd
int GetTradeDateInt()
{
	time_t now = ::time(NULL);
	return GetTradeDateInt(now);
}
//得到当前交易日,返回格式为yyyyMMdd
string GetTradeDate(time_t tTimeStamp)
{
	int iTradeDate = GetTradeDateInt(tTimeStamp);
	return to_string(iTradeDate);
	//char cRes[12] = { 0 };
	//itoa(iTradeDate, cRes, 10);
	//return cRes;
}
//得到当前交易日,返回格式为yyyyMMdd
string GetTradeDate()
{
	time_t now = ::time(NULL);
	return GetTradeDate(now);
}
//判断是否在清理缓存的时间段,只能在新交易日开始之前的20:00到20:30之间清理缓存。
//如果要修改清理缓存的时间,需要注意:上传TradeDayLIST文件的判断依据是当天是否有K线数据,因此清理缓存的时间不能在上传oss文件的时间之前.
bool IsInClearCacheTime(int& iMonth)
{
	time_t now = ::time(NULL);
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&now, &tms);
	iMonth = tms.tm_mon;
	if (tms.tm_hour == 19 && tms.tm_min >= 30)
	{
		int iYmd = (tms.tm_year + 1900) * 10000 + (tms.tm_mon + 1) * 100 + tms.tm_mday;
		//判断当天的日期与交易日的日期一致,由于清理缓存的时间在20:00到20:30之间,bClearCache参数为true的话,新交易日的分隔符在21点之后,所以下面条件能够成立
		if (iYmd == GetTradeDateInt(now, true))
			return true;
		//CString strDate;
		//strDate.Format("%d", iYmd);
		////判断当天的日期与交易日的日期一致
		//if (strDate.Compare(GetTradeDate(now).c_str()) == 0)
		//	return true;
	}
	return false;
}
//新交易日夜盘开始之前的20:00到20:30之间清理当天的tick、1分钟、5分钟和日线的缓存队列。
void ClearCache()
{
	static time_t s_LastClearCacheTime = 0;
	time_t now = ::time(NULL);
	int iMonth = 0;
	if (now - s_LastClearCacheTime >= 23 * 60 * 60 && IsInClearCacheTime(iMonth))
	{
		s_LastClearCacheTime = now;
		//更新当前新的交易日, 后面推送过来的行情里面的交易日如果不等于新的交易日,则不存入最近5分钟行情队列和200条全量队列
		g_iCurTradeDate = GetTradeDateInt();
		g_Logger->info("{}->清理当天数据缓存", __FUNCTION__);

		//清理tick去重队列
		try {
			g_lockUniqueTick.lock();
			g_Logger->info("清理去重队列: set={}, list={}", g_setUniqueTick.size(), g_listUniqueTick.size());
			set<CString>().swap(g_setUniqueTick);
			list<CString>().swap(g_listUniqueTick);
			g_lockUniqueTick.unlock();
		}
		catch (...)
		{
			g_lockUniqueTick.unlock();
			g_Logger->error("清理去重队列异常");
		}

		//清理近两百条全量tick缓存
		try {
			g_lock200QuanTick.lock();
			map<OptionID, list<CString>>().swap(g_map200QuanTick);
			g_lock200QuanTick.unlock();
		}
		catch (...)
		{
			g_lock200QuanTick.unlock();
			g_Logger->error("清理200条全量tick异常");
		}

		//清理当天未压缩和已压缩的增量tick缓存
		try {
			g_lockCompressTick.lock();
			set<OptionID>().swap(g_setUploadTickQuoteId);
			map<OptionID, string>().swap(g_mapCompressTick);
			map<OptionID, string>().swap(g_mapUnCompressTick);
			g_lockCompressTick.unlock();
		}
		catch (...)
		{
			g_lockCompressTick.unlock();
			g_Logger->error("清理增量tick异常");
		}

		//清理当天1分钟线的时间和根数
		try {
			g_lock1MinKlineCount.lock();
			map<OptionID, KLineCount>().swap(g_map1MinKlineCount);
			g_lock1MinKlineCount.unlock();
		}
		catch (...)
		{
			g_lock1MinKlineCount.unlock();
			g_Logger->error("清理1分钟K线计数异常");
		}

		//清理当天1分钟线
		try {
			g_lock1MinKline.lock();
			set<OptionID>().swap(g_setUpload1MQuoteId);
			map<OptionID, KLineCache>().swap(g_map1MinKlineDay);
			g_lock1MinKline.unlock();
		}
		catch (...)
		{
			g_lock1MinKline.unlock();
			g_Logger->error("清理1分钟K线异常");
		}

		//清理当天5分钟线的时间和根数
		try {
			g_lock5MinKlineCount.lock();
			map<OptionID, KLineCount>().swap(g_map5MinKlineCount);
			g_lock5MinKlineCount.unlock();
		}
		catch (...)
		{
			g_lock5MinKlineCount.unlock();
			g_Logger->error("清理5分钟K线计数异常");
		}

		//清理当天5分钟线
		try {
			g_lock5MinKline.lock();
			set<OptionID>().swap(g_setUpload5MQuoteId);
			map<OptionID, KLineCache>().swap(g_map5MinKline);
			g_lock5MinKline.unlock();
		}
		catch (...)
		{
			g_lock5MinKline.unlock();
			g_Logger->error("清理5分钟K线异常");
		}

		//清理日线
		try {
			g_lockDayKline.lock();
			set<OptionID>().swap(g_setUploadDayQuoteId);
			map<OptionID, KLine>().swap(g_mapDayKLine);
			g_lockDayKline.unlock();
		}
		catch (...)
		{
			g_lockDayKline.unlock();
			g_Logger->error("清理日线异常");
		}

		//清理原始行情缓存
		try {
			g_lockNativeQuotes.lock();
			g_Logger->info("清理原始行情缓存: {}", g_mapNativeQuotes.size());
			map<OptionID, string>().swap(g_mapNativeQuotes);
			set<OptionID>().swap(g_setUploadNativeQuoteId);
			g_lockNativeQuotes.unlock();
		}
		catch (...)
		{
			g_lockNativeQuotes.unlock();
			g_Logger->error("清理原始行情缓存异常");
		}

		//清理最近5分钟tick缓存
		try {
			g_lock5MinTick.lock();
			g_Logger->info("清理5分钟tick缓存: {}", g_map5MinTick.size());
			map<OptionID, string>().swap(g_map5MinTick);
			g_lock5MinTick.unlock();
		}
		catch (...)
		{
			g_lock5MinTick.unlock();
			g_Logger->error("清理5分钟tick缓存异常");
		}

		//清理最新行情缓存
		try {
			g_lockLastQuote.lock();
			g_Logger->info("清理最新行情缓存: {}", g_mapLastQuote.size());
			map<OptionID, HandledQuoteData>().swap(g_mapLastQuote);
			g_lockLastQuote.unlock();
		}
		catch (...)
		{
			g_lockLastQuote.unlock();
			g_Logger->error("清理最新行情缓存异常");
		}

		//清理客户端等待缓存服务器的应答数据
		try {
			g_lockClientKLineCache.lock();
			g_Logger->info("清理客户端K线缓存: {}", g_mapClientKLineCache.size());
			map<UINT32, ClientKLineCache>().swap(g_mapClientKLineCache);
			g_lockClientKLineCache.unlock();
		}
		catch (...)
		{
			g_lockClientKLineCache.unlock();
			g_Logger->error("清理客户端K线缓存异常");
		}

		//清除当天已发送短信的总条数
		try {
			CTcpPullClientImpl::s_iDXTotalNum = 0;

			//删除本地三天之前的历史数据(最长等待5分钟)
			g_Logger->info("{}->清理历史数据, 最多等待5分钟", __FUNCTION__);
			std::atomic<bool> cleanFinished(false);
			std::thread cleanThread([&cleanFinished]() {
				try {
					CleanHistoryData();
					cleanFinished = true;
				}
				catch (const std::exception& e) {
					g_Logger->error("清理历史数据异常: {}", e.what());
					cleanFinished = true;
				}
				catch (...) {
					g_Logger->error("清理历史数据异常(unknown)");
					cleanFinished = true;
				}
			});

			auto startWait = std::chrono::steady_clock::now();
			const int timeoutSeconds = 300; // 5分钟
			bool timeout = false;

			while (!cleanFinished) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::steady_clock::now() - startWait).count();

				if (elapsed >= timeoutSeconds) {
					timeout = true;
					g_Logger->warn("清理历史数据超时({}s)", timeoutSeconds);
					break;
				}
			}

			if (timeout) {
				cleanThread.detach();
				g_Logger->warn("清理历史数据超时, 后台继续执行");
			}
			else {
				if (cleanThread.joinable()) {
					cleanThread.join();
				}
				g_Logger->info("清理历史数据完成");
			}
		}
		catch (...)
		{
			g_Logger->error("清理历史数据线程异常");
		}

		g_Logger->info("{}->清理缓存流程完成", __FUNCTION__);

		//清理队列缓存
		try {
			size_t hqdyCount = g_queueHQDYData.clear();
			size_t handledCount = g_queueHandledData.clear();
			g_Logger->info("清理队列缓存: HQDY={}, Handled={}", hqdyCount, handledCount);
		}
		catch (...)
		{
			g_Logger->error("清理队列缓存异常");
		}

		//清理上传交易日历史记录, 防止集合过大
		try {
			if (g_setUploadTradeDate.size() > 100) {
				g_Logger->info("清理上传交易日缓存: {}", g_setUploadTradeDate.size());
				set<CString>().swap(g_setUploadTradeDate);
			}
		}
		catch (...)
		{
			g_Logger->error("清理上传交易日缓存异常");
		}

#ifdef __GLIBC__
		malloc_trim(0);  // 释放未使用的内存给操作系统
		g_Logger->info("{}->执行malloc_trim内存整理", __FUNCTION__);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		malloc_trim(0);
		g_Logger->info("{}->执行第二次malloc_trim", __FUNCTION__);
#endif
	/*	g_lockLastSetPrice.lock();
		for (auto &it : g_mapLastSetPrice)
		{
			it.second.second = it.second.first;
		}
		g_lockLastSetPrice.unlock();*/
	}
}
//将郑商所的合约代码从3位修改为4位
string ModifyZCEContractNo(const string& strContractNo)
{
	if (strContractNo.length() == 3 || strContractNo.length() == 5)
	{
		time_t tCurTime = ::time(NULL);
		struct tm* pinfo = localtime(&tCurTime);
		int iYear = pinfo->tm_year + 1900;
		CStringA strCurYear;
		strCurYear.Format("%d", iYear);
		if (strCurYear[3] == strContractNo[0])
			return string(1, strCurYear[2]) + strContractNo;
		else
		{
			iYear++;
			strCurYear.Format("%d", iYear);
			return string(1, strCurYear[2]) + strContractNo;
		}
	}
	return strContractNo;
}
//获取文件的本地路径 strDate的格式是yyyyMMdd
void GetLocalFilePath(const string& strExchage, const string& strComodity, const string& strContractNo, char cCallOrPutFlag, const string& strStrikePrice, FileType fileType, const string& strDate, string& path, string& fileName)
{
	string strConNo = strContractNo;
	if (strExchage.compare("ZCE") == 0 || strExchage.compare("zce") == 0)
		strConNo = ModifyZCEContractNo(strContractNo);
	CString strPath, strFileName;
	switch (fileType)
	{
	case FileType::TXT:
		strPath.Format("%s/%s/%s/%s/%s/", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["original"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.txt", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::TICK:
		strPath.Format("%s/%s/%s/%s/%s/", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.tick", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::M1:
		strPath.Format("%s/%s/%s/%s/%s/", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.1m", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::M5:
		strPath.Format("%s/%s/%s/%s/%s/", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.5m", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::DAY:
		strPath.Format("%s/%s/%s/%s/%s/", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.day", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	default:
		break;
	}

	path = strPath;
	fileName = strFileName;
}
//获取文件的阿里云OSS路径
void GetAliOSSFilePath(const string& strExchage, const string& strComodity, const string& strContractNo, char cCallOrPutFlag, const string& strStrikePrice, FileType fileType, const string& strDate, string& path, string& fileName)
{
	string strConNo = strContractNo;
	if (strExchage.compare("ZCE") == 0 || strExchage.compare("zce") == 0)
		strConNo = ModifyZCEContractNo(strContractNo);
	CString strPath, strFileName;;
	switch (fileType)
	{
	case FileType::TXT:
		strPath.Format("%s/%s/%s/%s/", (LPCSTR)g_mapProfile["original"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.txt", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::TICK:
		strPath.Format("%s/%s/%s/%s/", (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s_%s.tick", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str(), strDate.c_str());
		break;
	case FileType::M1:
		strPath.Format("%s/%s/%s/%s/", (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s.1m", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str());
		break;
	case FileType::M5:
		strPath.Format("%s/%s/%s/%s/", (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s.5m", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str());
		break;
	case FileType::DAY:
		strPath.Format("%s/%s/%s/%s/", (LPCSTR)g_mapProfile["dig"], strExchage.c_str(), strComodity.c_str(), strConNo.c_str());
		strFileName.Format("%s%c_%s.day", (LPCSTR)strPath, cCallOrPutFlag, strStrikePrice.c_str());
		break;
	default:
		break;
	}

	path = strPath;
	fileName = strFileName;
}
//提交给线程池的回调函数. 计算内外盘、均价、分钟线
void HandleQuoteProc(shared_ptr<HandledQuoteData> pHandledQuoteData)
{
	//将原始的行情解析成前端需要的行情格式
	CString strTick;
	bool bVolume = false;
	//HandleQuote返回false,表示直接丢弃该Quote,不做后续的推送和计算
	if (!HandleQuote(*pHandledQuoteData, strTick, false, bVolume))
		return;
	//根据客户端订阅的合约,判断是否向客户端推送行情
	OptionID optionid(pHandledQuoteData->strExchage, pHandledQuoteData->strComodity, pHandledQuoteData->strContractNo, pHandledQuoteData->cCallOrPutFlag, pHandledQuoteData->strStrikePrice);
	set<CONNID> setConnids;
	{
		CCriSecLock locallock(g_lockFrontEndConnids);
		setConnids = g_setFrontEndConnids;
	}
	if (setConnids.size() > 0)
	{
		CString strTick = pHandledQuoteData->ToQuanTick();
		string resultBuffer;
		//推送期权行情的时候，行情全文前面添加上OptionID
		ophq_packet_t hq_pkt;
		::memset(&hq_pkt, 0, sizeof(ophq_packet_t));
		memcpy(hq_pkt.exchange, pHandledQuoteData->strExchage.c_str(), pHandledQuoteData->strExchage.size());
		memcpy(hq_pkt.comodity, pHandledQuoteData->strComodity.c_str(), pHandledQuoteData->strComodity.size());
		memcpy(hq_pkt.contractNo, pHandledQuoteData->strContractNo.c_str(), pHandledQuoteData->strContractNo.size());
		hq_pkt.callOrPutFlag = pHandledQuoteData->cCallOrPutFlag;
		memcpy(hq_pkt.strikePrice, pHandledQuoteData->strStrikePrice.c_str(), pHandledQuoteData->strStrikePrice.size());
		//拼接包头
		front_packet_t packet;
		packet.cmd = cmd_OPHQ;
		packet.reqid = 0;
		packet.size = htonl(sizeof(ophq_packet_t) + strTick.size());
		resultBuffer = string((LPCSTR)&packet, sizeof(packet));
		resultBuffer.append((LPCSTR)&hq_pkt, sizeof(ophq_packet_t));
		resultBuffer.append(strTick);
		//遍历set并删除失效的连接.
		for (auto itConnid = setConnids.begin(); itConnid != setConnids.end(); ++itConnid)
			s_server.Send(*itConnid, (LPCBYTE)resultBuffer.c_str(), resultBuffer.size());
	}
	//分钟线计算规则: 在交易时段内或者收盘结束的10秒内 && 当日总成交量大于0 && 最新价不等于0 && 当前tick的总成交量>=上一个tick的总成交量
	if (IsInTradeTime(*pHandledQuoteData) && pHandledQuoteData->QTotalQty > 0 && pHandledQuoteData->QLastPrice > 0.0 && bVolume)
	{
		//计算K线
		CalKLine(*pHandledQuoteData);
	}
}
//计算K线
void CalKLine(const ExchangeQutoData& quoteData)
{
	OptionID quoteId(quoteData.strExchage, quoteData.strComodity, quoteData.strContractNo, quoteData.cCallOrPutFlag, quoteData.strStrikePrice);
	//判断该tick数据是否创下了日线里面的新高和新低
	//计算日线
	{
		g_lockDayKline.lock();
		auto itDay = g_mapDayKLine.find(quoteId);
		if (itDay != g_mapDayKLine.end() && itDay->second.IsValid())
		{
			itDay->second.dOpen = quoteData.QOpeningPrice;
			itDay->second.dHigh = quoteData.QHighPrice;
			itDay->second.dLow = quoteData.QLowPrice;
			itDay->second.dClose = quoteData.QLastPrice;
			itDay->second.i64Volume = quoteData.QTotalQty;
			itDay->second.i64TotalVolume = quoteData.QTotalQty;
			itDay->second.i64PositionQty = quoteData.QPositionQty;
		}
		else
		{
			//当天的第一个tick
			KLine& data = g_mapDayKLine[quoteId];
			data.strTradeDate = quoteData.strTradeDate;
			data.dOpen = quoteData.QOpeningPrice;
			data.dHigh = quoteData.QHighPrice;
			data.dLow = quoteData.QLowPrice;
			data.dClose = quoteData.QLastPrice;
			data.i64Volume = quoteData.QTotalQty;
			data.i64TotalVolume = quoteData.QTotalQty;
			data.i64PositionQty = quoteData.QPositionQty;
			data.iPect = QryJingDu(quoteId.strExchage, quoteId.strComodity);
		}
		g_lockDayKline.unlock();
	}
	//计算1分钟K线
	{
		int i1MinTime = quoteData.tTimeStamp / 60 * 60;
		g_lock1MinKline.lock();
		auto it1MinDay = g_map1MinKlineDay.find(quoteId);
		//不是当天第一根K线的判断标准: 已经有1min线的缓存数据 && (当天已经有已走完的K线 || 最近一根K线的时间戳不为0,说明有数据)
		if (it1MinDay != g_map1MinKlineDay.end() && (it1MinDay->second.mapCache.size() > 0 || it1MinDay->second.lastKLine.iTimeStamp != 0))
		{
			KLine& backData = it1MinDay->second.lastKLine;
			//K线没走完
			if (backData.iTimeStamp == i1MinTime)
			{
				if (backData.dHigh < quoteData.QLastPrice)
					backData.dHigh = quoteData.QLastPrice;
				if (quoteData.QLastPrice < backData.dLow)
					backData.dLow = quoteData.QLastPrice;
				//收盘价
				backData.dClose = quoteData.QLastPrice;
				//成交量
				backData.i64Volume += quoteData.QTotalQty - backData.i64TotalVolume;
				//当前总成交量
				backData.i64TotalVolume = quoteData.QTotalQty;
				//持仓量
				backData.i64PositionQty = quoteData.QPositionQty;
			}
			//K线已走完
			else
			{
				KLine data;
				data.iPect = QryJingDu(quoteId.strExchage, quoteId.strComodity);
				data.iTimeStamp = i1MinTime;
				data.strTradeDate = quoteData.strTradeDate;
				//开盘价
				data.dOpen = quoteData.QLastPrice;
				//最高价
				data.dHigh = quoteData.QLastPrice;
				//最低价
				data.dLow = quoteData.QLastPrice;
				//收盘价
				data.dClose = quoteData.QLastPrice;
				//成交量
				data.i64Volume = quoteData.QTotalQty - backData.i64TotalVolume;
				//当前总成交量
				data.i64TotalVolume = quoteData.QTotalQty;
				//持仓量
				data.i64PositionQty = quoteData.QPositionQty;
				//将已走完的K线添加到string缓存里面
				it1MinDay->second.mapCache[backData.iTimeStamp] = backData;
				//更新最新的未完成K线
				backData = data;
				//更新当天K线的时间和根数
				g_lock1MinKlineCount.lock();
				KLineCount& klineCount = g_map1MinKlineCount[quoteId];
				if (klineCount.mapTimeCount.size() > 0)
				{
					UINT32 iCount = klineCount.mapTimeCount.begin()->second;
					klineCount.mapTimeCount[i1MinTime] = iCount + 1;
				}
				else
				{
					klineCount.mapTimeCount[i1MinTime] = 1;
				}
				g_lock1MinKlineCount.unlock();
			}
		}
		else
		{
			//当天第一根K线
			KLine data;
			data.iPect = QryJingDu(quoteId.strExchage, quoteId.strComodity);
			data.iTimeStamp = i1MinTime;
			data.strTradeDate = quoteData.strTradeDate;
			//开盘价用的是第一个tick的开盘价
			data.dOpen = quoteData.QOpeningPrice;
			//最高价用 开盘价和最新价里面的高者
			data.dHigh = quoteData.QOpeningPrice > quoteData.QLastPrice ? quoteData.QOpeningPrice : quoteData.QLastPrice;
			//最低价用 开盘价和最新价里面的低者
			data.dLow = quoteData.QOpeningPrice < quoteData.QLastPrice ? quoteData.QOpeningPrice : quoteData.QLastPrice;
			//收盘价
			data.dClose = quoteData.QLastPrice;
			//成交量
			data.i64Volume = quoteData.QTotalQty;
			//当前总成交量
			data.i64TotalVolume = quoteData.QTotalQty;
			//持仓量
			data.i64PositionQty = quoteData.QPositionQty;
			KLineCache& dataDay = g_map1MinKlineDay[quoteId];
			dataDay.lastKLine = data;
			//更新当天K线的时间和根数
			g_lock1MinKlineCount.lock();
			KLineCount& klineCount = g_map1MinKlineCount[quoteId];
			klineCount.mapTimeCount[i1MinTime] = 1;
			g_lock1MinKlineCount.unlock();
		}
		g_lock1MinKline.unlock();
	}
	//计算5分钟K线
	{
		int i5MinTime = quoteData.tTimeStamp / 300 * 300;
		g_lock5MinKline.lock();
		auto it5Min = g_map5MinKline.find(quoteId);
		//不是当天第一根K线的判断标准: 已经有5min线的缓存数据 && (当天已经有已走完的K线 || 最近一根K线的时间戳不为0,说明有数据)
		if (it5Min != g_map5MinKline.end() && (it5Min->second.mapCache.size() > 0 || it5Min->second.lastKLine.iTimeStamp != 0))
		{
			KLine& backData = it5Min->second.lastKLine;
			//K线没走完
			if (backData.iTimeStamp == i5MinTime)
			{
				if (backData.dHigh < quoteData.QLastPrice)
					backData.dHigh = quoteData.QLastPrice;
				if (quoteData.QLastPrice < backData.dLow)
					backData.dLow = quoteData.QLastPrice;
				//收盘价
				backData.dClose = quoteData.QLastPrice;
				//成交量
				backData.i64Volume += quoteData.QTotalQty - backData.i64TotalVolume;
				//当前总成交量
				backData.i64TotalVolume = quoteData.QTotalQty;
				//持仓量
				backData.i64PositionQty = quoteData.QPositionQty;
			}
			//K线已走完
			else
			{
				KLine data;
				data.iPect = QryJingDu(quoteId.strExchage, quoteId.strComodity);
				data.iTimeStamp = i5MinTime;
				data.strTradeDate = quoteData.strTradeDate;
				//开盘价
				data.dOpen = quoteData.QLastPrice;
				//最高价
				data.dHigh = quoteData.QLastPrice;
				//最低价
				data.dLow = quoteData.QLastPrice;
				//收盘价
				data.dClose = quoteData.QLastPrice;
				//成交量
				data.i64Volume = quoteData.QTotalQty - backData.i64TotalVolume;
				//当前总成交量
				data.i64TotalVolume = quoteData.QTotalQty;
				//持仓量
				data.i64PositionQty = quoteData.QPositionQty;
				//将已走完的K线添加到string缓存里面
				it5Min->second.mapCache[backData.iTimeStamp] = backData;
				//更新最新的未完成K线
				backData = data;
				//更新当天K线的时间和根数
				g_lock5MinKlineCount.lock();
				KLineCount& klineCount = g_map5MinKlineCount[quoteId];
				if (klineCount.mapTimeCount.size() > 0)
				{
					UINT32 iCount = klineCount.mapTimeCount.begin()->second;
					klineCount.mapTimeCount[i5MinTime] = iCount + 1;
				}
				else
				{
					klineCount.mapTimeCount[i5MinTime] = 1;
				}
				g_lock5MinKlineCount.unlock();
			}
		}
		else
		{
			//当天第一根K线
			KLine data;
			data.iPect = QryJingDu(quoteId.strExchage, quoteId.strComodity);
			data.iTimeStamp = i5MinTime;
			data.strTradeDate = quoteData.strTradeDate;
			//开盘价用的是第一个tick的开盘价
			data.dOpen = quoteData.QOpeningPrice;
			//最高价用 开盘价和最新价里面的高者
			data.dHigh = quoteData.QOpeningPrice > quoteData.QLastPrice ? quoteData.QOpeningPrice : quoteData.QLastPrice;
			//最低价用 开盘价和最新价里面的低者
			data.dLow = quoteData.QOpeningPrice < quoteData.QLastPrice ? quoteData.QOpeningPrice : quoteData.QLastPrice;
			//收盘价
			data.dClose = quoteData.QLastPrice;
			//成交量
			data.i64Volume = quoteData.QTotalQty;
			//当前总成交量
			data.i64TotalVolume = quoteData.QTotalQty;
			//持仓量
			data.i64PositionQty = quoteData.QPositionQty;
			KLineCache& data5Min = g_map5MinKline[quoteId];
			data5Min.lastKLine = data;
			//更新当天K线的时间和根数
			g_lock5MinKlineCount.lock();
			KLineCount& klineCount = g_map5MinKlineCount[quoteId];
			klineCount.mapTimeCount[i5MinTime] = 1;
			g_lock5MinKlineCount.unlock();
		}
		g_lock5MinKline.unlock();
	}
}
//将本地存储的全量tick数据恢复成HandledQuoteData结构
bool ParseHandledQuote(vector<CString>& vecTickData, HandledQuoteData& quoteData)
{
	quoteData.strTradeDate = vecTickData[0];
	quoteData.strExchage = vecTickData[1];
	quoteData.strComodity = vecTickData[2].MakeUpper();
	quoteData.strContractNo = vecTickData[3];
	quoteData.cCallOrPutFlag = vecTickData[4][0];
	quoteData.strStrikePrice = vecTickData[5];
	quoteData.tTimeStamp = atoll(vecTickData[6]);
	quoteData.strMilisec = vecTickData[7];
	quoteData.QPreClosingPrice = atof(vecTickData[8]);
	quoteData.QPreSettlePrice = atof(vecTickData[9]);
	quoteData.QPrePositionQty = atoll(vecTickData[10]);
	quoteData.QOpeningPrice = atof(vecTickData[11]);
	quoteData.QLastPrice = atof(vecTickData[12]);
	quoteData.QHighPrice = atof(vecTickData[13]);
	quoteData.QLowPrice = atof(vecTickData[14]);
	quoteData.QLimitUpPrice = atof(vecTickData[15]);
	quoteData.QLimitDownPrice = atof(vecTickData[16]);
	quoteData.QTotalQty = atoll(vecTickData[17]);
	quoteData.QPositionQty = atoll(vecTickData[18]);
	quoteData.QClosingPrice = atof(vecTickData[19]);
	quoteData.QSettlePrice = atof(vecTickData[20]);
	quoteData.QLastQty = atoll(vecTickData[21]);
	for (size_t i = 0; i < 5; ++i)
		quoteData.QBidPrice[i] = atof(vecTickData[22 + i]);
	for (size_t i = 0; i < 5; ++i)
		quoteData.QBidQty[i] = atoll(vecTickData[27 + i]);
	for (size_t i = 0; i < 5; ++i)
		quoteData.QAskPrice[i] = atof(vecTickData[32 + i]);
	for (size_t i = 0; i < 5; ++i)
		quoteData.QAskQty[i] = atoll(vecTickData[37 + i].TrimRight());
	quoteData.iNeiPan = atoi(vecTickData[42]);
	quoteData.iWaiPan = atoi(vecTickData[43]);
	quoteData.dAvgPrice = atof(vecTickData[44]);
}

bool is16()
{
	time_t timep;
	time_t now = time(&timep);
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&now, &tms);
	if (tms.tm_hour >= 9 && tms.tm_hour <= 16)
		return false;
	return true;
}
//恢复最近200条全量tick和当天全部增量tick
void RecoverTick(const OptionID& optionID, bool bOnlyM1Month)
{
	string strCurTradeDate = GetTradeDate();
	string strWholeTick;
	list<string> listTick;
	//先从本地文件读
	string localPath, localFileName;
	GetLocalFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::TICK, strCurTradeDate, localPath, localFileName);
	ifstream ifs(localFileName, ios::in);
	if (ifs.is_open())
	{
		string s;
		while (getline(ifs, s))
		{
			strWholeTick += s + "\n";
			listTick.push_back(std::move(s));
		}
		ifs.close();
	}
	//else
	//{
	//	//本地文件打开失败,则从阿里云读
	//	string ossPath, ossFileName;
	//	GetAliOSSFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::TICK, strCurTradeDate, ossPath, ossFileName);
	//	string strUnCompress;
	//	if (GetAndUnComFromOSS(ossFileName, strUnCompress))
	//	{
	//		stringstream ssTick(strUnCompress);
	//		string s;
	//		while (getline(ssTick, s))
	//		{
	//			strWholeTick += s + "\n";
	//			listTick.push_back(std::move(s));
	//		}
	//	}
	//}

	//计算近200条全量tick
	vector<CString> vecLastQuanTick;
	list<vector<CString>> list200Tick;
	for (auto itList = listTick.begin(); itList != listTick.end(); ++itList)
	{
		vector<CString> vecTickData;
		SplitString(itList->c_str(), vecTickData, "|");
		//注意此处长度应该是43,比原始行情数据多了三个字段,内盘,外盘和均价
		if (vecTickData.size() == 45)
		{
			//第一条默认是全量的数据
			if (vecLastQuanTick.size() == 0)
			{
				//如果第一条数据的最新成交量大于0,则添加进list200Tick
				if (atoi(vecTickData[21]) > 0)
					list200Tick.push_back(vecTickData);
			}
			else
			{
				//根据最近一条数据将增量数据恢复成全量数据
				for (int i = 0; i < vecTickData.size(); ++i)
				{
					//如果为空,则用上一条tick里面的值
					if (vecTickData[i].IsEmpty())
						vecTickData[i] = vecLastQuanTick[i];
				}
				//如果新一条数据的当日总成交量 != 上一条数据的当日总成交量, 则把该tick添加进list200Tick
				if (vecTickData[17].Compare(vecLastQuanTick[17]) != 0)
				{
					list200Tick.push_back(vecTickData);
					if (list200Tick.size() > 200)
						list200Tick.pop_front();
				}
			}
			vecLastQuanTick = vecTickData;
		}
	}
	//if (list200Tick.size() > 0)
	//{
	//	//恢复每个合约最新一条的tick数据,否则1.开盘第一根分钟线的成交量会计算错误2.指数的开盘价也会计算错误,会出现跳开		
	//	HandledQuoteData& handledData = g_mapLastQuote[optionID];
	//	ParseHandledQuote(list200Tick.back(), handledData);
	//	g_lockLastQuote.lock();
	//	if (is16())
	//	{
	//		g_mapLastSetPrice[optionID].first = handledData.QSettlePrice;
	//		g_mapLastSetPrice[optionID].second = handledData.QSettlePrice;
	//	}
	//	else {
	//		g_mapLastSetPrice[optionID].first = handledData.QPreSettlePrice;
	//		g_mapLastSetPrice[optionID].second = handledData.QPreSettlePrice;
	//	}
	//	g_lockLastQuote.unlock();
	//}
	//如果不在清理当天缓存的时间段内,才恢复以下这些数据结构
	if (!bOnlyM1Month)
	{
		//恢复最近200条全量tick
		g_lock200QuanTick.lock();
		list<CString>& listQuanTick = g_map200QuanTick[optionID];
		for (vector<CString>& vecTicks : list200Tick)
		{
			CString strQuanTick;
			for (const CString& str : vecTicks)
				strQuanTick += str + "|";
			strQuanTick.Trim("|");
			strQuanTick += "\n";
			listQuanTick.push_back(strQuanTick);
		}
		g_lock200QuanTick.unlock();
		//恢复当天全部增量tick
		g_lockCompressTick.lock();
		string strUncompressYick;
		//如果配置文件里面storage=false 需要先把 交易所|品种(处理过的添加了XYZ)|合约\n  添加到压缩包的前面,方便返回给客户端
		if (!g_bStorageServer)
			strUncompressYick = optionID.ToString() + strWholeTick;
		else
			strUncompressYick = strWholeTick;
		g_mapUnCompressTick[optionID] = strUncompressYick;
		CompressTick(optionID, strUncompressYick);
		g_lockCompressTick.unlock();
	}
}
//恢复当天1分钟缓存
void RecoverM1(const OptionID& optionID, bool bOnlyM1Month)
{
	string strCurTradeDate = GetTradeDate();
	bool bLocalM1Exist = true;
	//当天的1分钟从本地读
	list<string> listDayM1;
	string localPath, localFileName;
	GetLocalFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::M1, strCurTradeDate, localPath, localFileName);
	ifstream ifs(localFileName, ios::in);
	if (ifs.is_open())
	{
		string s;
		while (getline(ifs, s))
			listDayM1.push_back(s);
		ifs.close();
	}
	else
	{
		bLocalM1Exist = false;
	}
	//本地文件不存在则从阿里云读
	//if (!bLocalM1Exist)
	//{
	//	string ossPath, ossFileName;
	//	GetAliOSSFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::M1, strCurTradeDate, ossPath, ossFileName);
	//	string strUnCompress;
	//	if (GetAndUnComFromOSS(ossFileName, strUnCompress))
	//	{
	//		stringstream ssTick(strUnCompress);
	//		string s;
	//		while (getline(ssTick, s))
	//		{
	//			if (s.find(strCurTradeDate) != string::npos)
	//				listDayM1.push_back(s);
	//		}
	//	}
	//}
	int pect = QryJingDu(optionID.strExchage, optionID.strComodity);
	g_lock1MinKline.lock();
	if (!bOnlyM1Month && listDayM1.size() > 0)
	{
		KLineCache& dayCache = g_map1MinKlineDay[optionID];
		g_lock1MinKlineCount.lock();
		for (const string& strLine : listDayM1)
		{
			std::vector<CString> vecField;
			SplitString(strLine, vecField, "|");
			if (vecField.size() == 8)
			{
				UINT32 iTimeStamp = atoi(vecField[1]);
				dayCache.mapCache[iTimeStamp] = KLine(vecField[0], iTimeStamp, atof(vecField[2]), atof(vecField[3]), atof(vecField[4]), atof(vecField[5]), vecField[6], vecField[7], pect);
				//初始化1分钟K线的时间和根数
				KLineCount& klineCount = g_map1MinKlineCount[optionID];
				if (klineCount.mapTimeCount.size() > 0)
				{
					UINT32 iCount = klineCount.mapTimeCount.begin()->second;
					klineCount.mapTimeCount[iTimeStamp] = iCount + 1;
				}
				else
				{
					klineCount.mapTimeCount[iTimeStamp] = 1;
				}
			}
		}
		g_lock1MinKlineCount.unlock();
		//恢复LastKLine,否则盘中重启之后,计算出的第一根K线的成交量会出错
		if (dayCache.mapCache.size() > 0)
		{
			//恢复LastKLine里面的 当日总成交量 .计算方法: 把所有已走完K线的成交量相加
			UINT64 i64TotalVolume = 0;
			for (auto itcache = dayCache.mapCache.begin(); itcache != dayCache.mapCache.end(); ++itcache)
				i64TotalVolume += itcache->second.i64Volume;
			auto itBegin = dayCache.mapCache.begin();
			dayCache.lastKLine = itBegin->second;
			dayCache.mapCache.erase(itBegin);
			dayCache.lastKLine.i64TotalVolume = i64TotalVolume;
		}
	}
	g_lock1MinKline.unlock();
}
//恢复当天5分钟缓存
void RecoverM5(const OptionID& optionID, bool bOnlyM1Month)
{
	string strCurTradeDate = GetTradeDate();
	bool bLocalM5Exist = true;
	//当天的1分钟从本地读
	list<string> listDayM5;
	string localPath, localFileName;
	GetLocalFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::M5, strCurTradeDate, localPath, localFileName);
	ifstream ifs(localFileName, ios::in);
	if (ifs.is_open())
	{
		string s;
		while (getline(ifs, s))
			listDayM5.push_back(s);
		ifs.close();
	}
	else
	{
		bLocalM5Exist = false;
	}
	//本地文件不存在则从阿里云读
	//if (!bLocalM5Exist)
	//{
	//	string ossPath, ossFileName;
	//	GetAliOSSFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::M5, strCurTradeDate, ossPath, ossFileName);
	//	string strUnCompress;
	//	if (GetAndUnComFromOSS(ossFileName, strUnCompress))
	//	{
	//		stringstream ssTick(strUnCompress);
	//		string s;
	//		while (getline(ssTick, s))
	//		{
	//			if (s.find(strCurTradeDate) != string::npos)
	//				listDayM5.push_back(s);
	//		}
	//	}
	//}
	int pect = QryJingDu(optionID.strExchage, optionID.strComodity);
	g_lock5MinKline.lock();
	if (!bOnlyM1Month && listDayM5.size() > 0)
	{
		KLineCache& dayCache = g_map5MinKline[optionID];
		g_lock5MinKlineCount.lock();
		for (const string& strLine : listDayM5)
		{
			std::vector<CString> vecField;
			SplitString(strLine, vecField, "|");
			if (vecField.size() == 8)
			{
				UINT32 iTimeStamp = atoi(vecField[1]);
				dayCache.mapCache[iTimeStamp] = KLine(vecField[0], iTimeStamp, atof(vecField[2]), atof(vecField[3]), atof(vecField[4]), atof(vecField[5]), vecField[6], vecField[7], pect);
				//初始化5分钟K线的时间和根数
				KLineCount& klineCount = g_map5MinKlineCount[optionID];
				if (klineCount.mapTimeCount.size() > 0)
				{
					UINT32 iCount = klineCount.mapTimeCount.begin()->second;
					klineCount.mapTimeCount[iTimeStamp] = iCount + 1;
				}
				else
				{
					klineCount.mapTimeCount[iTimeStamp] = 1;
				}
			}
		}
		g_lock5MinKlineCount.unlock();
		//恢复LastKLine,否则盘中重启之后,计算出的第一根K线的成交量会出错
		if (dayCache.mapCache.size() > 0)
		{
			//恢复LastKLine里面的 当日总成交量 .计算方法: 把所有已走完K线的成交量相加
			UINT64 i64TotalVolume = 0;
			for (auto itcache = dayCache.mapCache.begin(); itcache != dayCache.mapCache.end(); ++itcache)
				i64TotalVolume += itcache->second.i64Volume;
			auto itBegin = dayCache.mapCache.begin();
			dayCache.lastKLine = itBegin->second;
			dayCache.mapCache.erase(itBegin);
			dayCache.lastKLine.i64TotalVolume = i64TotalVolume;
		}
	}
	g_lock5MinKline.unlock();
}
//恢复当天日线缓存
void RecoverDay(const OptionID& optionID, bool bOnlyM1Month)
{
	string strCurTradeDate = GetTradeDate();
	bool bLocalDayExist = true;
	//当天的1分钟从本地读
	string strDay;
	string localPath, localFileName;
	GetLocalFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::DAY, strCurTradeDate, localPath, localFileName);
	ifstream ifs(localFileName, ios::in);
	if (ifs.is_open())
	{
		string s;
		while (getline(ifs, s))
		{
			if (!s.empty())
			{
				strDay = std::move(s);
				break;
			}
		}
		ifs.close();
	}
	else
	{
		bLocalDayExist = false;
	}
	int pect = QryJingDu(optionID.strExchage, optionID.strComodity);
	//本地文件不存在则从阿里云读
	//if (!bLocalDayExist)
	//{
	//	string ossPath, ossFileName;
	//	GetAliOSSFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::DAY, strCurTradeDate, ossPath, ossFileName);
	//	string strUnCompress;
	//	if (GetAndUnComFromOSS(ossFileName, strUnCompress))
	//	{
	//		stringstream ssTick(strUnCompress);
	//		string s;
	//		while (getline(ssTick, s))
	//		{
	//			if (s.find(strCurTradeDate) != string::npos)
	//			{
	//				strDay = std::move(s);
	//				break;
	//			}
	//		}
	//	}
	//}
	if (!bOnlyM1Month && !strDay.empty())
	{
		std::vector<CString> vecField;
		SplitString(strDay, vecField, "|");
		if (vecField.size() == 7)
		{
			g_lockDayKline.lock();
			g_mapDayKLine[optionID] = KLine(vecField[0], 0, atof(vecField[1]), atof(vecField[2]), atof(vecField[3]), atof(vecField[4]), vecField[5], vecField[6], pect);
			g_lockDayKline.unlock();
		}
	}
}
list<std::function<void()>> s_listRecoverFunctors;
mutex s_lockRecoverFunctors;
const int s_iRecoverThreadCount = 8;
void RecoverThreadProc(CountDownLatch* pCountDownLatch)
{
	while (true)
	{
		//从队列里面取出一个任务,如果队列为空,则线程结束
		std::function<void()> task;
		{
			lock_guard<mutex> lock(s_lockRecoverFunctors);
			//如果队列为空,则线程退出
			if (s_listRecoverFunctors.empty())
			{
				break;
			}
			else
			{
				task = std::move(s_listRecoverFunctors.front());
				s_listRecoverFunctors.pop_front();
			}
		}
		//执行任务
		task();
	}
	//退出之前,执行countDown
	pCountDownLatch->countDown();
}
//缓存恢复.五个数据结构: 最近200条全量tick, 当天所有的增量tick, 当天1分钟线, 当月1分钟线
//近200条全量tick是从当天所有的增量tick里面计算恢复的
void RecoverCache(bool bOnlyM1Month)
{
#ifndef __DEBUG__
	g_Logger->info("{}->开始恢复缓存...", __FUNCTION__);
	//将全市场品种列表拷贝到临时链表里面
	CReadLock readlock(g_lockHeyueLiebiao);
	map<ContractID, list<OptionID>> mapOptionIDs(g_mapOptionIDs);
	readlock.unlock();
	//开始遍历品种列表
	for (auto itMap = mapOptionIDs.begin(); itMap != mapOptionIDs.end(); ++itMap)
	{
		for (const OptionID& optionID : itMap->second)
		{
			s_listRecoverFunctors.push_back(bind(RecoverTick, optionID, bOnlyM1Month));
			s_listRecoverFunctors.push_back(bind(RecoverM1, optionID, bOnlyM1Month));
			s_listRecoverFunctors.push_back(bind(RecoverM5, optionID, bOnlyM1Month));
			s_listRecoverFunctors.push_back(bind(RecoverDay, optionID, bOnlyM1Month));
		}
	}
	CountDownLatch countDownLatch(s_iRecoverThreadCount);
	//创建线程执行缓存恢复任务
	for (size_t i = 0; i < s_iRecoverThreadCount; i++)
	{
		std::thread t(RecoverThreadProc, &countDownLatch);
		t.detach();
	}
	//等待所有的线程执行完成
	countDownLatch.wait();
	g_Logger->info("{}->缓存恢复完成.", __FUNCTION__);
#endif // __DEBUG__

	//缓存初始化已经完成, 通知主线程
	std::lock_guard<std::mutex> guard(g_lockInitCache);
	g_cvInitCache.notify_one();
}
//判断comodityID当前是否已经收盘
bool IsClose(const ComodityID& comodityID, int iHourMin)
{
	bool bRes = false;
	CReadLock readlock(g_lockProductTradeTime);
	auto itMap = g_mapProductTradeTime.find(comodityID);
	if (itMap != g_mapProductTradeTime.end())
	{
		//找到最后收盘的那个时间段
		STimeSegment times(0, 0);
		for (const STimeSegment& timeSegment : itMap->second)
		{
			if (timeSegment.From < 210000 && timeSegment.To > times.To)
				times = timeSegment;
		}
		//判断当前时间是否超过了收盘时间
		if (iHourMin > times.To + 10)
			bRes = true;
	}
	else
	{
		g_Logger->error("{}->找不到品种{}-{}的有效交易时间", __FUNCTION__, (LPCSTR)comodityID.strExchage, (LPCSTR)comodityID.strComodity);
	}
	return bRes;
}
//判断当前时间是否是本交易日的所有品种的收盘时间
bool IsTradeDayAllClose(const CString& strCurTradeDate)
{
	bool bRes = true;
	time_t now = time(NULL);
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&now, &tms);
	int iYmd = (tms.tm_year + 1900) * 10000 + (tms.tm_mon + 1) * 100 + tms.tm_mday;
	CString strDate;
	strDate.Format("%d", iYmd);
	//判断当天的日期与交易日的日期一致
	if (strDate.Compare(strCurTradeDate.c_str()) == 0)
	{
		int iHourMin = tms.tm_hour * 10000 + tms.tm_min * 100 + tms.tm_sec;
		CReadLock readlock(g_lockHeyueLiebiao);
		map<ContractID, list<OptionID>> mapOptionIDs(g_mapOptionIDs);
		readlock.unlock();
		//判断每个品种是否都在收盘时间
		for (auto itMap = mapOptionIDs.begin(); itMap != mapOptionIDs.end(); ++itMap)
		{
			if (!IsClose(itMap->first, iHourMin))
			{
				bRes = false;
				break;
			}
		}
	}
	else
	{
		bRes = false;
	}
	return bRes;
}
//根据服务器启动时间来判断是否已经上传过阿里云
bool HasUpload(const CString& strCurTradeDate)
{
	bool bRes = false;
	string strStartTradeDate = GetTradeDate(g_tServerStartTime);
	if (strCurTradeDate.compare(strStartTradeDate) == 0)
	{
		//服务器启动时间所在的交易日与当前时间所在的交易日相同,并且服务器启动时间在15点之后,则认为数据已经上传过
		struct tm tms = { 0 };
		localtime_r(&g_tServerStartTime, &tms);
		int iYmd = (tms.tm_year + 1900) * 10000 + (tms.tm_mon + 1) * 100 + tms.tm_mday;
		CString strDate;
		strDate.Format("%d", iYmd);
		if (strDate.CompareNoCase(strStartTradeDate.c_str()) == 0 && tms.tm_hour >= 15 && tms.tm_hour < 20)
			bRes = true;
	}
	return bRes;
}
//判断是否在上传时间段,只在15点-18:59点之间上传
bool IsInUploadTime()
{
	time_t timep;
	time_t now = time(&timep);
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&now, &tms);
	if (15 <= tms.tm_hour && tms.tm_hour <= 18)
		return true;
	return false;
}
//追加内容到指定的oss文件
bool AppendAliOSS(const string& strOssFileName, const string& strContent)
{
	if (strContent.size() == 0)
		return true;
	bool bRes = false;
	//先下载并解压
	string strUnCompress;
	do
	{
		string strCompress;
		if (!AliOSSUtil::IsFileExist(strOssFileName))
		{
			g_Logger->info("{}->OSS上{}文件不存在!", __FUNCTION__, strOssFileName.c_str());
			//阿里云上文件不存在,则内容是当前要追加的内容
			strUnCompress = strContent;
		}
		else
		{
			if (!AliOSSUtil::DownloadFile(strOssFileName, strCompress))
			{
				g_Logger->critical("{}->从OSS获取{}文件失败!", __FUNCTION__, strOssFileName.c_str());
				break;
			}
			int iRet = UnCompressData(strCompress, strUnCompress);
			if (-1 == iRet)
			{
				g_Logger->error("{}->推测 Gzip 解压结果长度失败! {}文件并非有效的 Gzip 格式.", __FUNCTION__, strOssFileName.c_str());
				break;
			}
			else if (0 == iRet)
			{
				//追加文件内容
				strUnCompress += "\n" + strContent;
			}
			else
			{
				g_Logger->error("{}->{}文件解压出错,错误代码:{}", __FUNCTION__, strOssFileName.c_str(), iRet);
			}
		}
	} while (false);
	//重新压缩 交易所_品种_MI_LIST.config并上传
	if (strUnCompress.size() > 0)
	{
		string strCompress;
		int iRet = CompressData(strUnCompress, strCompress);
		if (0 == iRet)
		{
			//上传到oss
			if (!AliOSSUtil::UploadFile(strOssFileName, strCompress))
				g_Logger->critical("{}->向{}追加内容时上传文件失败!", __FUNCTION__, strOssFileName.c_str());
			else
				bRes = true;
		}
		else
			g_Logger->error("{}->向{}追加内容时压缩出错,错误代码:{}", __FUNCTION__, strOssFileName.c_str(), iRet);
	}
	return bRes;
}
//从后台页面获取品种的有效交易时间信息
bool GetOptionExpireDate(string& strRes)
{
	//先判断配置文件里面是否有期权过期日的地址
	auto itProfile = g_mapProfile.find("optionExpireDate");
	if (itProfile == g_mapProfile.end())
	{
		g_Logger->info("{}->配置文件里面没有optionExpireDate项(期权过期日的地址)!", __FUNCTION__);
		return false;
	}
	//发起http请求
	bool bSucess = false;
	for (int i = 0; i < 3; ++i)
	{
		if (CURLE_OK == HttpCommonGet((LPCSTR)itProfile->second, strRes))
		{
			bSucess = true;
			break;
		}
	}
	if (bSucess)
		g_Logger->info("{}->获取期权过期日信息成功", __FUNCTION__);
	else
		g_Logger->info("{}->获取期权过期日信息失败", __FUNCTION__);
	return bSucess;
}
//在合约列表里面添加上期权的到期日. 必须在g_lockHeyueLiebiao的写锁范围内调用此函数
void removeMS(std::string& str) {
	std::string target = "MS";
	size_t pos = str.find(target);
	while (pos != std::string::npos) {
		str.erase(pos, target.length());
		pos = str.find(target, pos);
	}
}


//bool endsWith(const std::string& str, const std::string& suffix) {
//	if (str.length() < suffix.length()) {
//		return false;
//	}
//	return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
//}
//
//// Helper function to generate JSON based on filter condition
//std::string generateJson(const map<string, map<string, ContractExpDateStrike>>& mapTree, bool includeMS, bool includeNonMS) {
//	rapidjson::Document document;
//	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
//	rapidjson::Value rootValue(rapidjson::kObjectType);
//
//	//exchange是一个数组
//	rapidjson::Value excArrayValue(rapidjson::kArrayType);
//
//	for (auto itTree = mapTree.begin(); itTree != mapTree.end(); ++itTree) {
//		rapidjson::Value excValue(rapidjson::kObjectType);
//
//		//cmdt是一个数组
//		rapidjson::Value comoArrayValue(rapidjson::kArrayType);
//
//		for (auto itExc = itTree->second.begin(); itExc != itTree->second.end(); ++itExc) {
//			rapidjson::Value comoValue(rapidjson::kObjectType);
//
//			//contract是一个数组            
//			rapidjson::Value conArrayValue(rapidjson::kArrayType);
//
//			for (auto itCon = itExc->second.begin(); itCon != itExc->second.end(); ++itCon) {
//				// Check if the contract code ends with "MS"
//				bool isMS = endsWith(itCon->first, "MS");
//
//				// Skip if filtering conditions are not met
//				if ((isMS && !includeMS) || (!isMS && !includeNonMS)) {
//					continue;
//				}
//
//				rapidjson::Value conValue(rapidjson::kObjectType);
//
//				//strike是一个数组
//				rapidjson::Value strikeArrayValue(rapidjson::kArrayType);
//
//				for (const std::string& strStrike : itCon->second.setStrike) {
//					strikeArrayValue.PushBack(rapidjson::StringRef(strStrike.c_str()), allocator);
//				}
//
//				conValue.AddMember(rapidjson::StringRef("code"), rapidjson::StringRef(itCon->first.c_str()), allocator);
//				conValue.AddMember(rapidjson::StringRef("ContractExpDate"), rapidjson::StringRef(itCon->second.strExpDate.c_str()), allocator);
//				conValue.AddMember(rapidjson::StringRef("strike"), strikeArrayValue, allocator);
//
//				conArrayValue.PushBack(conValue, allocator);
//			}
//
//			// Only add commodity if it has contracts after filtering
//			if (conArrayValue.Size() > 0) {
//				comoValue.AddMember(rapidjson::StringRef("code"), rapidjson::StringRef(itExc->first.c_str()), allocator);
//				comoValue.AddMember(rapidjson::StringRef("contract"), conArrayValue, allocator);
//				comoArrayValue.PushBack(comoValue, allocator);
//			}
//		}
//
//		// Only add exchange if it has commodities after filtering
//		if (comoArrayValue.Size() > 0) {
//			excValue.AddMember(rapidjson::StringRef("code"), rapidjson::StringRef(itTree->first.c_str()), allocator);
//			excValue.AddMember(rapidjson::StringRef("cmdt"), comoArrayValue, allocator);
//			excArrayValue.PushBack(excValue, allocator);
//		}
//	}
//
//	rootValue.AddMember(rapidjson::StringRef("exchange"), excArrayValue, allocator);
//
//	//序列化为json字符串
//	rapidjson::StringBuffer buffer;
//	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
//	rootValue.Accept(writer);
//
//	return std::string(buffer.GetString());
//}
//
//// Helper function to save string to file
//bool saveToFile(const std::string& content, const std::string& filename) {
//	FILE* file = fopen(filename.c_str(), "w+");
//	if (!file) {
//		std::cerr << "Failed to open file: " << filename << std::endl;
//		return false;
//	}
//
//	fprintf(file, "%s", content.c_str());
//	fclose(file);
//	return true;
//}

void UpdateHYLBExpireDate(CStringA& strHYLBJson)
{
	time_t tCurTime = ::time(NULL);
	struct tm* pinfo = localtime(&tCurTime);
	int iCurYear = pinfo->tm_year + 1900;
	struct ExpDateStrike
	{
		string strExpDate;
		set<string> setStrike;
	};
	//key是合约代码2101,value是到期日+行权价格
	typedef map<string, ExpDateStrike> ContractExpDateStrike;
	//生成树形结构,key是交易所, value是map<品种,map<合约代码,到期日+行权价格>>
	map<string, map<string, ContractExpDateStrike>> mapTree;
	CStringA strJson;
	if (GetOptionExpireDate(strJson))
	{
		rapidjson::Document doc;
		doc.Parse<rapidjson::kParseDefaultFlags>(strJson);
		if (doc.HasParseError())
		{
			g_Logger->error("{}->parse json error:{}", __FUNCTION__, (LPCSTR)strJson);
			return;
		}
		auto &data = doc["data"];
		if (!data.IsArray())
		{
			g_Logger->error("{}->data is not array!", __FUNCTION__);
			return;
		}
		//保存每个合约对应的到期日
		map<CStringA, CStringA> mapInstrIdExpDate;
		for (size_t i = 0; i < data.Size(); ++i)
		{
			CStringA strInstrId = data[i]["underlyinginstrid"].GetString();
			strInstrId.MakeUpper();
			CStringA strExpireDate = data[i]["expiredate"].GetString();
			mapInstrIdExpDate[strInstrId] = strExpireDate;
		}
		//解析期权合约列表
		map<ContractID, list<OptionID>> mapOptionIDs;
		mapOptionIDs = g_mapOptionIDs;
		for (auto itMap = mapOptionIDs.begin(); itMap != mapOptionIDs.end(); ++itMap)
		{
			CStringA strInstrId = itMap->first.strComodity + itMap->first.strContractNo;
			CStringA strExpDate;
			auto itExpDate = mapInstrIdExpDate.find(strInstrId);
			if (itExpDate != mapInstrIdExpDate.end())
			{
				strExpDate = itExpDate->second;
			}
			else
			{
				//根据合约代码手动计算合约到期日。 
				CString strDate;
				//对郑商所三位的合约代码需要特殊处理.需要补上年份的前三位
				if (itMap->first.strContractNo.GetLength() == 3 || itMap->first.strContractNo.GetLength() == 5)
				{
					int iYear = iCurYear;
					CStringA strCurYear;
					strCurYear.Format("%d", iYear);
					if (strCurYear[3] == itMap->first.strContractNo[0])
					{
						strDate = strCurYear.Left(3) + itMap->first.strContractNo;
					}
					else
					{
						iYear++;
						strCurYear.Format("%d", iYear);
						strDate = strCurYear.Left(3) + itMap->first.strContractNo;
					}
				}
				//四位的合约代码只需要补上年份的前两位
				else if (itMap->first.strContractNo.GetLength() == 4)
				{
					CStringA strCurYear;
					strCurYear.Format("%d", iCurYear);
					strDate = strCurYear.Left(2) + itMap->first.strContractNo;
				}
				//月份合约代码里面都有,天数固定就是1号
				strDate += "01";
				removeMS(strDate);
				strExpDate = strDate;
				g_Logger->error("{}->找不到{}的期权到期日!", __FUNCTION__, strInstrId);
			}

			map<string, ContractExpDateStrike>& mapExchange = mapTree[itMap->first.strExchage];
			ContractExpDateStrike& mapConExpDateStrike = mapExchange[itMap->first.strComodity];
			ExpDateStrike& expDateStrike = mapConExpDateStrike[itMap->first.strContractNo];
			expDateStrike.strExpDate = strExpDate;
			for (auto itList = itMap->second.begin(); itList != itMap->second.end(); ++itList)
			{
				expDateStrike.setStrike.insert(itList->strStrikePrice);
			}
		}
		rapidjson::Document docAll, docMS, docNonMS;
		docAll.SetObject();
		docMS.SetObject();
		docNonMS.SetObject();

		// Get allocators for each document
		rapidjson::Document::AllocatorType& allocAll = docAll.GetAllocator();
		rapidjson::Document::AllocatorType& allocMS = docMS.GetAllocator();
		rapidjson::Document::AllocatorType& allocNonMS = docNonMS.GetAllocator();

		// Create exchange arrays for each document
		rapidjson::Value excArrayAll(rapidjson::kArrayType);
		rapidjson::Value excArrayMS(rapidjson::kArrayType);
		rapidjson::Value excArrayNonMS(rapidjson::kArrayType);

		// Iterate over exchanges
		for (auto itTree = mapTree.begin(); itTree != mapTree.end(); ++itTree) {
			rapidjson::Value excValueAll(rapidjson::kObjectType);
			rapidjson::Value excValueMS(rapidjson::kObjectType);
			rapidjson::Value excValueNonMS(rapidjson::kObjectType);

			rapidjson::Value comoArrayAll(rapidjson::kArrayType);
			rapidjson::Value comoArrayMS(rapidjson::kArrayType);
			rapidjson::Value comoArrayNonMS(rapidjson::kArrayType);

			// Iterate over commodities (cmdt)
			for (auto itExc = itTree->second.begin(); itExc != itTree->second.end(); ++itExc) {
				rapidjson::Value comoValueAll(rapidjson::kObjectType);
				rapidjson::Value comoValueMS(rapidjson::kObjectType);
				rapidjson::Value comoValueNonMS(rapidjson::kObjectType);

				rapidjson::Value conArrayAll(rapidjson::kArrayType);
				rapidjson::Value conArrayMS(rapidjson::kArrayType);
				rapidjson::Value conArrayNonMS(rapidjson::kArrayType);

				// Iterate over contracts
				for (auto itCon = itExc->second.begin(); itCon != itExc->second.end(); ++itCon) {
					const std::string& contractCode = itCon->first;
					// Check if contract code ends with "MS"
					bool isMS = contractCode.size() >= 2 && contractCode.substr(contractCode.size() - 2) == "MS";

					// Create contract object for "all" document
					rapidjson::Value conValueAll(rapidjson::kObjectType);
					rapidjson::Value strikeArrayAll(rapidjson::kArrayType);

					for (const std::string& strStrike : itCon->second.setStrike) {
						rapidjson::Value strikeValue;
						strikeValue.SetString(strStrike.c_str(), strStrike.length(), allocAll);
						strikeArrayAll.PushBack(strikeValue, allocAll);
					}

					// Add members to "all" contract
					rapidjson::Value codeValueAll;
					codeValueAll.SetString(contractCode.c_str(), contractCode.length(), allocAll);
					conValueAll.AddMember("code", codeValueAll, allocAll);

					rapidjson::Value expDateValueAll;
					expDateValueAll.SetString(itCon->second.strExpDate.c_str(), itCon->second.strExpDate.length(), allocAll);
					conValueAll.AddMember("ContractExpDate", expDateValueAll, allocAll);

					conValueAll.AddMember("strike", strikeArrayAll, allocAll);

					// Always add to the "all" array
					conArrayAll.PushBack(conValueAll, allocAll);

					// Add to MS or non-MS array based on the condition
					if (isMS) {
						// Create contract object for MS document
						rapidjson::Value conValueMS(rapidjson::kObjectType);
						rapidjson::Value strikeArrayMS(rapidjson::kArrayType);

						for (const std::string& strStrike : itCon->second.setStrike) {
							rapidjson::Value strikeValue;
							strikeValue.SetString(strStrike.c_str(), strStrike.length(), allocMS);
							strikeArrayMS.PushBack(strikeValue, allocMS);
						}

						// Add members to MS contract
						rapidjson::Value codeValueMS;
						codeValueMS.SetString(contractCode.c_str(), contractCode.length(), allocMS);
						conValueMS.AddMember("code", codeValueMS, allocMS);

						rapidjson::Value expDateValueMS;
						expDateValueMS.SetString(itCon->second.strExpDate.c_str(), itCon->second.strExpDate.length(), allocMS);
						conValueMS.AddMember("ContractExpDate", expDateValueMS, allocMS);

						conValueMS.AddMember("strike", strikeArrayMS, allocMS);

						conArrayMS.PushBack(conValueMS, allocMS);
					}
					else {
						// Create contract object for non-MS document
						rapidjson::Value conValueNonMS(rapidjson::kObjectType);
						rapidjson::Value strikeArrayNonMS(rapidjson::kArrayType);

						for (const std::string& strStrike : itCon->second.setStrike) {
							rapidjson::Value strikeValue;
							strikeValue.SetString(strStrike.c_str(), strStrike.length(), allocNonMS);
							strikeArrayNonMS.PushBack(strikeValue, allocNonMS);
						}

						// Add members to non-MS contract
						rapidjson::Value codeValueNonMS;
						codeValueNonMS.SetString(contractCode.c_str(), contractCode.length(), allocNonMS);
						conValueNonMS.AddMember("code", codeValueNonMS, allocNonMS);

						rapidjson::Value expDateValueNonMS;
						expDateValueNonMS.SetString(itCon->second.strExpDate.c_str(), itCon->second.strExpDate.length(), allocNonMS);
						conValueNonMS.AddMember("ContractExpDate", expDateValueNonMS, allocNonMS);

						conValueNonMS.AddMember("strike", strikeArrayNonMS, allocNonMS);

						conArrayNonMS.PushBack(conValueNonMS, allocNonMS);
					}
				}

				// Add to MS commodity array if there are MS contracts
				if (!conArrayMS.Empty()) {
					rapidjson::Value codeValueMS;
					codeValueMS.SetString(itExc->first.c_str(), itExc->first.length(), allocMS);
					comoValueMS.AddMember("code", codeValueMS, allocMS);
					comoValueMS.AddMember("contract", conArrayMS, allocMS);
					comoArrayMS.PushBack(comoValueMS, allocMS);
				}

				// Add to non-MS commodity array if there are non-MS contracts
				if (!conArrayNonMS.Empty()) {
					rapidjson::Value codeValueNonMS;
					codeValueNonMS.SetString(itExc->first.c_str(), itExc->first.length(), allocNonMS);
					comoValueNonMS.AddMember("code", codeValueNonMS, allocNonMS);
					comoValueNonMS.AddMember("contract", conArrayNonMS, allocNonMS);
					comoArrayNonMS.PushBack(comoValueNonMS, allocNonMS);
				}

				// Always add to the "all" commodity array
				rapidjson::Value codeValueAll;
				codeValueAll.SetString(itExc->first.c_str(), itExc->first.length(), allocAll);
				comoValueAll.AddMember("code", codeValueAll, allocAll);
				comoValueAll.AddMember("contract", conArrayAll, allocAll);
				comoArrayAll.PushBack(comoValueAll, allocAll);
			}

			// Add to MS exchange array if there are MS commodities
			if (!comoArrayMS.Empty()) {
				rapidjson::Value codeValueMS;
				codeValueMS.SetString(itTree->first.c_str(), itTree->first.length(), allocMS);
				excValueMS.AddMember("code", codeValueMS, allocMS);
				excValueMS.AddMember("cmdt", comoArrayMS, allocMS);
				excArrayMS.PushBack(excValueMS, allocMS);
			}

			// Add to non-MS exchange array if there are non-MS commodities
			if (!comoArrayNonMS.Empty()) {
				rapidjson::Value codeValueNonMS;
				codeValueNonMS.SetString(itTree->first.c_str(), itTree->first.length(), allocNonMS);
				excValueNonMS.AddMember("code", codeValueNonMS, allocNonMS);
				excValueNonMS.AddMember("cmdt", comoArrayNonMS, allocNonMS);
				excArrayNonMS.PushBack(excValueNonMS, allocNonMS);
			}

			// Always add to the "all" exchange array
			rapidjson::Value codeValueAll;
			codeValueAll.SetString(itTree->first.c_str(), itTree->first.length(), allocAll);
			excValueAll.AddMember("code", codeValueAll, allocAll);
			excValueAll.AddMember("cmdt", comoArrayAll, allocAll);
			excArrayAll.PushBack(excValueAll, allocAll);
		}

		// Add exchange arrays to their respective documents
		docAll.AddMember("exchange", excArrayAll, allocAll);
		docMS.AddMember("exchange", excArrayMS, allocMS);
		docNonMS.AddMember("exchange", excArrayNonMS, allocNonMS);

		// Serialize to JSON strings
		rapidjson::StringBuffer bufferAll, bufferMS, bufferNonMS;
		rapidjson::Writer<rapidjson::StringBuffer> writerAll(bufferAll);
		rapidjson::Writer<rapidjson::StringBuffer> writerMS(bufferMS);
		rapidjson::Writer<rapidjson::StringBuffer> writerNonMS(bufferNonMS);

		docAll.Accept(writerAll);
		docMS.Accept(writerMS);
		docNonMS.Accept(writerNonMS);

		// Save to local files with error handling
		try {
			std::ofstream fileAll("./contractJson.txt");
			if (!fileAll.is_open()) {
				throw std::runtime_error("Failed to open contractJson.txt for writing");
			}
			g_strHeyueLiebiao = std::string(bufferAll.GetString());
			fileAll << bufferAll.GetString();
			fileAll.close();

			std::ofstream fileMS("./contractJson_MS.txt");
			if (!fileMS.is_open()) {
				throw std::runtime_error("Failed to open contractJson_MS.txt for writing");
			}

			g_strMSHeyueLiebiao = std::string(bufferMS.GetString());
			fileMS << bufferMS.GetString();
			fileMS.close();

			std::ofstream fileNonMS("./contractJson_nonMS.txt");
			if (!fileNonMS.is_open()) {
				throw std::runtime_error("Failed to open contractJson_nonMS.txt for writing");
			}
			g_strOPHeyueLiebiao = std::string(bufferNonMS.GetString());
			fileNonMS << bufferNonMS.GetString();
			fileNonMS.close();
		}
		catch (const std::exception& e) {
			// Handle file writing errors
			std::cerr << "Error writing JSON files: " << e.what() << std::endl;
		}
	}
}

//Add option expiration date to NHYLB contract list
void UpdateNHYLBExpireDate(CStringA& strNHYLBJson)
{
	time_t tCurTime = ::time(NULL);
	struct tm* pinfo = localtime(&tCurTime);
	int iCurYear = pinfo->tm_year + 1900;
	
	// Get option expiration date data
	CStringA strExpDateJson;
	map<CStringA, CStringA> mapInstrIdExpDate;
	
	if (GetOptionExpireDate(strExpDateJson))
	{
		rapidjson::Document docExpDate;
		docExpDate.Parse<rapidjson::kParseDefaultFlags>(strExpDateJson);
		if (!docExpDate.HasParseError() && docExpDate.HasMember("data") && docExpDate["data"].IsArray())
		{
			auto &data = docExpDate["data"];
			for (size_t i = 0; i < data.Size(); ++i)
			{
				if (data[i].HasMember("underlyinginstrid") && data[i].HasMember("expiredate"))
				{
					CStringA strInstrId = data[i]["underlyinginstrid"].GetString();
					strInstrId.MakeUpper();
					CStringA strExpireDate = data[i]["expiredate"].GetString();
					mapInstrIdExpDate[strInstrId] = strExpireDate;
				}
			}
		}
	}
	
	// Parse NHYLB JSON
	rapidjson::Document doc;
	doc.Parse<rapidjson::kParseDefaultFlags>(strNHYLBJson);
	if (doc.HasParseError())
	{
		g_Logger->error("{}->parse NHYLB json error!", __FUNCTION__);
		return;
	}
	
	if (!doc.HasMember("exchange") || !doc["exchange"].IsArray())
	{
		g_Logger->error("{}->NHYLB json has no exchange array!", __FUNCTION__);
		return;
	}
	
	// Iterate over exchanges
	auto& exchanges = doc["exchange"];
	for (rapidjson::SizeType i = 0; i < exchanges.Size(); ++i)
	{
		if (!exchanges[i].HasMember("cmdt") || !exchanges[i]["cmdt"].IsArray())
			continue;
			
		auto& cmdts = exchanges[i]["cmdt"];
		// Iterate over commodities
		for (rapidjson::SizeType j = 0; j < cmdts.Size(); ++j)
		{
			if (!cmdts[j].HasMember("code") || !cmdts[j].HasMember("contract") || !cmdts[j]["contract"].IsArray())
				continue;
				
			CStringA strComodity = cmdts[j]["code"].GetString();
			auto& contracts = cmdts[j]["contract"];
			
			// Iterate over contracts
			for (rapidjson::SizeType k = 0; k < contracts.Size(); ++k)
			{
				if (!contracts[k].HasMember("code"))
					continue;
					
				CStringA strContractCode = contracts[k]["code"].GetString();
				CStringA strInstrId = strComodity + strContractCode;
				strInstrId.MakeUpper();
				
				CStringA strExpDate;
				auto itExpDate = mapInstrIdExpDate.find(strInstrId);
				if (itExpDate != mapInstrIdExpDate.end())
				{
					strExpDate = itExpDate->second;
				}
				else
				{
					// Calculate contract expiration date manually based on contract code
					CString strDate;
					// Special handling for ZCE 3-digit contract codes, need to add first 3 digits of year
					if (strContractCode.GetLength() == 3 || strContractCode.GetLength() == 5)
					{
						int iYear = iCurYear;
						CStringA strCurYear;
						strCurYear.Format("%d", iYear);
						if (strCurYear[3] == strContractCode[0])
						{
							strDate = strCurYear.Left(3) + strContractCode;
						}
						else
						{
							iYear++;
							strCurYear.Format("%d", iYear);
							strDate = strCurYear.Left(3) + strContractCode;
						}
					}
					// For 4-digit contract codes, only need to add first 2 digits of year
					else if (strContractCode.GetLength() == 4)
					{
						CStringA strCurYear;
						strCurYear.Format("%d", iCurYear);
						strDate = strCurYear.Left(2) + strContractCode;
					}else if (strContractCode.GetLength()==6)
					{
						CStringA strCurYear;
						strCurYear.Format("%d", iCurYear);
						strDate = strCurYear.Left(2) + strContractCode.Left(4);
					}
					// Month is in contract code, day is fixed as 1st
					strDate += "01";
					removeMS(strDate);
					strExpDate = strDate;
					g_Logger->warn("{}->Cannot find option expiration date for {}, using calculated value:{}", __FUNCTION__, (LPCSTR)strInstrId, (LPCSTR)strExpDate);
				}
				
				// Add ContractExpDate field
				rapidjson::Value expDateValue;
				expDateValue.SetString(strExpDate, strExpDate.GetLength(), doc.GetAllocator());
				contracts[k].AddMember("ContractExpDate", expDateValue, doc.GetAllocator());
			}
		}
	}
	
	// Convert modified JSON back to string
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	
	// Update the passed parameter
	strNHYLBJson = buffer.GetString();
	
	g_Logger->info("{}->NHYLB option expiration date update completed", __FUNCTION__);
}

//更新Option/Contract/TradeDay.LIST文件
bool UpdateTradeDayList(const CString& strCurTradeDate)
{
	CString strTradeDayListFile;
	strTradeDayListFile.Format("%s/Contract/TradeDay.LIST", g_mapProfile["dig"].c_str());
	return AppendAliOSS(strTradeDayListFile.c_str(), strCurTradeDate.c_str());
}
//上传合约列表
void UploadHeyueLiebiao(const CString& strCurTradeDate)
{
	//判断是否已经上传过数据
	if (g_setUploadTradeDate.find(strCurTradeDate) != g_setUploadTradeDate.end())
		return;
	CStringA strHeyueLiebiao;
	CReadLock readlock(g_lockHeyueLiebiao);
	UpdateHYLBExpireDate(g_strHeyueLiebiao);
	strHeyueLiebiao = g_strHeyueLiebiao;
	readlock.unlock();
	string strCompress;
	int iRet = CompressData(strHeyueLiebiao, strCompress);
	if (0 == iRet)
	{
		//上传到oss
		CString strHeyuePath;
		strHeyuePath.Format("%s/Contract/%s/%s.all", g_mapProfile["dig"].c_str(), strCurTradeDate.Left(4).c_str(), strCurTradeDate.c_str());
		if (!AliOSSUtil::UploadFile(strHeyuePath, strCompress))
			g_Logger->critical("{}->向OSS上传{}文件失败!", __FUNCTION__, strHeyuePath.c_str());
		//先判断是否需要上传TradeDay.LIST.根据当天是否有K线数据
		bool bNeedUpload = false;
		g_lock5MinKline.lock();
		if (g_map5MinKline.size() > 0)
			bNeedUpload = true;
		g_lock5MinKline.unlock();
		if (bNeedUpload)
		{
			if (!UpdateTradeDayList(strCurTradeDate))
				g_Logger->critical("{}->向OSS更新TradeDay.LIST文件失败!", __FUNCTION__);
		}
		else
			g_Logger->info("{}->无需上传TradeDay.LIST文件,因为{}没有K线数据,不是交易日!", __FUNCTION__, strCurTradeDate.c_str());
		g_setUploadTradeDate.insert(strCurTradeDate);
	}
	else
		g_Logger->error("{}->合约列表上传压缩出错,错误代码:{}", __FUNCTION__, iRet);

	// 上传NHYLB
	CStringA strNHeyueLiebiao;
	CReadLock readlockN(g_lockNHeyueLiebiao);
	UpdateNHYLBExpireDate(g_strNHeyueLiebiao); // 确保与 HYLB 逻辑一致
	strNHeyueLiebiao = g_strNHeyueLiebiao;
	readlockN.unlock();

	string strCompressN;
	int iRetN = CompressData(strNHeyueLiebiao, strCompressN);
	if (0 == iRetN)
	{
		CString strNHeyuePath;
		// NHYLB 应该是: "%s/NContract/%s/%s.all" -> dig/NContract/2026/20260126.all
		strNHeyuePath.Format("%s/NContract/%s/%s.all", g_mapProfile["dig"].c_str(), strCurTradeDate.Left(4).c_str(), strCurTradeDate.c_str());

		if (!AliOSSUtil::UploadFile(strNHeyuePath, strCompressN))
			g_Logger->critical("{}->向OSS上传{}文件失败!", __FUNCTION__, strNHeyuePath.c_str());
		else
			g_Logger->info("{}->向OSS上传{}文件成功!", __FUNCTION__, strNHeyuePath.c_str());
	}
	else
	{
		g_Logger->error("{}->NHYLB合约列表上传压缩出错,错误代码:{}", __FUNCTION__, iRetN);
	}
}
//上传当天增量tick  
void UploadTick(const CString& strCurTradeDate)
{
	//先把当前5分钟地tick增量数据压缩进当天的增量tick数据里面
	Save5MinTick();
	//分别将压缩过的tick数据上传
	g_lockCompressTick.lock();
	for (auto itCompress = g_mapCompressTick.begin(); itCompress != g_mapCompressTick.end(); ++itCompress)
	{
		//判断是否已经上传过,如果已经上传了,则不再上传
		if (g_setUploadTickQuoteId.find(itCompress->first) != g_setUploadTickQuoteId.end())
			continue;
		string ossPath, ossFileName;
		GetAliOSSFilePath(itCompress->first.strExchage, itCompress->first.strComodity, itCompress->first.strContractNo, itCompress->first.cCallOrPutFlag, itCompress->first.strStrikePrice, FileType::TICK, strCurTradeDate, ossPath, ossFileName);
		if (!ossFileName.empty())
		{
			//上传到oss
			if (!AliOSSUtil::UploadFile(ossFileName, itCompress->second))
				g_Logger->critical("{}->向OSS上传{}文件失败!", __FUNCTION__, ossFileName.c_str());
			else
				//上传成功,则记录下已经上传成功的QuoteID
				g_setUploadTickQuoteId.insert(itCompress->first);
		}
	}
	g_lockCompressTick.unlock();
}
//上传分钟线和日线数据
void UploadKLine(const CString& strCurTradeDate)
{
	//分别将1分钟线数据追加到阿里云oss
	g_lock1MinKline.lock();
	for (auto it1M = g_map1MinKlineDay.begin(); it1M != g_map1MinKlineDay.end(); ++it1M)
	{
		//判断是否已经上传过,如果已经上传了,则不再上传
		if (g_setUpload1MQuoteId.find(it1M->first) != g_setUpload1MQuoteId.end())
			continue;
		string ossPath, ossFileName;
		GetAliOSSFilePath(it1M->first.strExchage, it1M->first.strComodity, it1M->first.strContractNo, it1M->first.cCallOrPutFlag, it1M->first.strStrikePrice, FileType::M1, strCurTradeDate, ossPath, ossFileName);
		string strContent(it1M->second.ToString());
		if (!ossFileName.empty() && AppendAliOSS(ossFileName, strContent))
			//上传成功,则记录下已经上传成功的QuoteID
			g_setUpload1MQuoteId.insert(it1M->first);
	}
	g_lock1MinKline.unlock();
	//分别将5M线数据追加到阿里云oss
	g_lock5MinKline.lock();
	for (auto it5M = g_map5MinKline.begin(); it5M != g_map5MinKline.end(); ++it5M)
	{
		//判断是否已经上传过,如果已经上传了,则不再上传
		if (g_setUpload5MQuoteId.find(it5M->first) != g_setUpload5MQuoteId.end())
			continue;
		string ossPath, ossFileName;
		GetAliOSSFilePath(it5M->first.strExchage, it5M->first.strComodity, it5M->first.strContractNo, it5M->first.cCallOrPutFlag, it5M->first.strStrikePrice, FileType::M5, strCurTradeDate, ossPath, ossFileName);
		string strContent(it5M->second.ToString());
		if (!ossFileName.empty() && AppendAliOSS(ossFileName, strContent))
			//上传成功,则记录下已经上传成功的QuoteID
			g_setUpload5MQuoteId.insert(it5M->first);
	}
	g_lock5MinKline.unlock();
	//分别将日线数据追加到阿里云oss
	g_lockDayKline.lock();
	for (auto itDay = g_mapDayKLine.begin(); itDay != g_mapDayKLine.end(); ++itDay)
	{
		//判断是否已经上传过,如果已经上传了,则不再上传
		if (g_setUploadDayQuoteId.find(itDay->first) != g_setUploadDayQuoteId.end())
			continue;
		string ossPath, ossFileName;
		GetAliOSSFilePath(itDay->first.strExchage, itDay->first.strComodity, itDay->first.strContractNo, itDay->first.cCallOrPutFlag, itDay->first.strStrikePrice, FileType::DAY, strCurTradeDate, ossPath, ossFileName);
		string strContent;
		if (itDay->second.IsValid())
			strContent = itDay->second.ToDayString();
		if (!ossFileName.empty() && AppendAliOSS(ossFileName, strContent))
			//上传成功,则记录下已经上传成功的QuoteID
			g_setUploadDayQuoteId.insert(itDay->first);
	}
	g_lockDayKline.unlock();
}
//上传原始数据文件
void UploadNativeData(const CString& strCurTradeDate)
{
	g_Logger->info("{}->开始上传原始数据...", __FUNCTION__);
	//将全市场品种列表拷贝到临时链表里面
	CReadLock readlock(g_lockHeyueLiebiao);
	map<ContractID, list<OptionID>> mapOptionIDs(g_mapOptionIDs);
	readlock.unlock();
	//开始遍历品种列表
	for (auto itMap = mapOptionIDs.begin(); itMap != mapOptionIDs.end(); ++itMap)
	{
		//原始数据就是从转发服务接收到的行情信息
		for (const OptionID& optionID : itMap->second)
		{
			if (g_setUploadNativeQuoteId.find(optionID) != g_setUploadNativeQuoteId.end())
				continue;
			//先将本地文件的内容读进来
			string localPath, localFileName;
			GetLocalFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::TXT, strCurTradeDate, localPath, localFileName);
			std::ifstream iFile(localFileName, ios::in);
			if (iFile.is_open())
			{
				stringstream buf;
				buf << iFile.rdbuf();
				iFile.close();
				string strFileContent = buf.str();
				if (strFileContent.size() > 0)
				{
					string strOssPath, strOssFileName;
					GetAliOSSFilePath(optionID.strExchage, optionID.strComodity, optionID.strContractNo, optionID.cCallOrPutFlag, optionID.strStrikePrice, FileType::TXT, strCurTradeDate, strOssPath, strOssFileName);
					//压缩
					string strCompress;
					int iRet = CompressData(strFileContent, strCompress);
					if (0 == iRet)
					{
						//上传到oss
						if (!AliOSSUtil::UploadFile(strOssFileName, strCompress))
							g_Logger->critical("{}->向阿里云{}上传文件失败!", __FUNCTION__, strOssFileName.c_str());
					}
					else
						g_Logger->error("{}->向阿里云{}上传时压缩出错,错误代码:{}", __FUNCTION__, strOssFileName.c_str(), iRet);
				}
				g_setUploadNativeQuoteId.insert(optionID);
			}
		}
	}
	g_Logger->info("{}->原始数据上传完成.", __FUNCTION__);
}
//如果已经收盘，就计算主力合约并上传
void UploadDataToAliOSS()
{
	CString strCurTradeDate = GetTradeDate();
	bool bAllClose = IsTradeDayAllClose(strCurTradeDate);
	//全部品种都收盘了
	if (bAllClose)
	{
		//是存储服务器 && 主服务器 && 还没有上传过阿里云 && 在上传时间之内;  才上传数据
		if (g_bStorageServer && g_bIsMain && !HasUpload(strCurTradeDate) && IsInUploadTime())
		{
			//将合约列表上传阿里云并更新TradeDay.LIST文件
			UploadHeyueLiebiao(strCurTradeDate);
			//上传当天增量tick 
			UploadTick(strCurTradeDate);
			//上传分钟线和日线数据
			UploadKLine(strCurTradeDate);
			//上传原始数据文件
			UploadNativeData(strCurTradeDate);
			g_Logger->info("{} complete.", __FUNCTION__);
		}
	}
}
//----------------字符串解码--------------------//
const std::map<string, int>::value_type dict_value[] =
{
	std::map<string, int>::value_type("MA", 0),
	std::map<string, int>::value_type("cQ", 1),
	std::map<string, int>::value_type("cw", 2),
	std::map<string, int>::value_type("Yw", 3),
	std::map<string, int>::value_type("Zg", 4),
	std::map<string, int>::value_type("dA", 5),
	std::map<string, int>::value_type("aA", 6),
	std::map<string, int>::value_type("bQ", 7),
	std::map<string, int>::value_type("aw", 8),
	std::map<string, int>::value_type("bw", 9),
	std::map<string, int>::value_type("Ow", 10)
};
const std::map<string, string>::value_type space_dict_value[] =
{
	std::map<string, string>::value_type("MA", ""),
	std::map<string, string>::value_type("cQ", "="),
	std::map<string, string>::value_type("cw", "==")
};
string DecodeText(const string& strRule)
{
	map<string, int> dict(dict_value, dict_value + 11);
	map<string, string> spaceDict(space_dict_value, space_dict_value + 3);
	//先计算要删掉的字符串的长度
	string strTemp = strRule.substr(0, 2);
	map<string, int>::iterator dictIt = dict.find(strTemp);
	if (dict.end() == dictIt)
		return "";
	int iShouldDeleteLen = dictIt->second + 2;
	//删掉头部的随机字符串
	string strDecode = strRule.substr(iShouldDeleteLen, strRule.length() - iShouldDeleteLen);
	//末尾添加等号
	strTemp = strDecode.substr(0, 2);
	map<string, string>::iterator it = spaceDict.find(strTemp);
	if (spaceDict.end() == it)
		return "";
	strTemp = it->second;
	strDecode = strDecode.substr(2, strDecode.length() - 2) + strTemp;
	//Base64解码
	DWORD dwDestLen = SYS_GuessBase64DecodeBound((LPCBYTE)strDecode.c_str(), strDecode.size());
	if (dwDestLen == 0)
		return "";
	BYTE* lpszDest = new BYTE[dwDestLen];
	if (lpszDest == nullptr)
		return "";
	memset(lpszDest, 0, dwDestLen);
	if (0 == SYS_Base64Decode((LPCBYTE)strDecode.c_str(), strDecode.size(), lpszDest, dwDestLen))
	{
		string strResult((LPCSTR)lpszDest, dwDestLen);
		delete[] lpszDest;
		//字符串里面的每个字符都按位取反
		for (int i = 0; i < strResult.length(); ++i)
		{
			strResult[i] = ~strResult[i];
		}
		return strResult;
	}
	else
	{
		delete[] lpszDest;
		return "";
	}
}
//获取指定目录下所有的文件,递归遍历子目录. <目录名,文件名>存放到listFiles里面, 将空的目录保存到listEmptyDirs结构里面
void listDir(const string& strPath, list<pair<string, string>>& listFiles, list<string>& listEmptyDirs)
{
	DIR              *pDir;
	struct dirent    *ent;

	pDir = opendir(strPath.c_str());
	if (pDir == NULL)
		return;
	bool bEmpty = true;
	while ((ent = readdir(pDir)) != NULL)
	{
		if (ent->d_type & DT_DIR)
		{
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			bEmpty = false;
			stringstream ssChildPath;
			ssChildPath << strPath << "/" << ent->d_name;
			//cout << ssChildPath.str() << endl;			
			listDir(ssChildPath.str(), listFiles, listEmptyDirs);
		}
		else
		{
			bEmpty = false;
			listFiles.push_back(make_pair(strPath, ent->d_name));
			//cout << ent->d_name << endl;
		}
	}
	closedir(pDir);
	if (bEmpty)
		listEmptyDirs.push_back(strPath);
}
void DeleteFile(const string& strPath)
{
	//拼接rm -rf命令
	ostringstream buffer;
	buffer << "rm -rf " << strPath;
	//调用命令删除该文件夹
	system(buffer.str().c_str());
}
void listDirAndClean(const string& strPath, int iYmd, int& deletedFiles, int& emptyDirs)
{
	DIR              *pDir;
	struct dirent    *ent;

	pDir = opendir(strPath.c_str());
	if (pDir == NULL)
		return;

	bool bEmpty = true;
	while ((ent = readdir(pDir)) != NULL)
	{
		if (ent->d_type & DT_DIR)
		{
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			bEmpty = false;
			stringstream ssChildPath;
			ssChildPath << strPath << "/" << ent->d_name;
			listDirAndClean(ssChildPath.str(), iYmd, deletedFiles, emptyDirs);
		}
		else
		{
			bEmpty = false;
			string fileName = ent->d_name;

			bool shouldDelete = false;

			// file name like: CFFEX_TF_09_20190531.5m
			int index0 = fileName.rfind("_");
			if (index0 != string::npos)
			{
				int index1 = fileName.find(".", index0);
				if (index1 != string::npos)
				{
					int iDirYmd = atoi(fileName.substr(index0 + 1, index1).c_str());
					if (iDirYmd < iYmd)
						shouldDelete = true;
				}
			}
			// file name like: 20190605.tick
			else
			{
				int index1 = fileName.find(".");
				if (index1 != string::npos)
				{
					int iDirYmd = atoi(fileName.substr(0, index1).c_str());
					if (iDirYmd < iYmd)
						shouldDelete = true;
				}
			}

			if (shouldDelete)
			{
				string fullPath = strPath + "/" + fileName;
				DeleteFile(fullPath);
				deletedFiles++;

				if (deletedFiles % 1000 == 0)
				{
					g_Logger->info("已删除 {} 个历史文件...", deletedFiles);
				}
			}
		}
	}
	closedir(pDir);

	if (bEmpty)
	{
		DeleteFile(strPath);
		emptyDirs++;
	}
}void DeleteOldFiles(int iYmd, const list<pair<string, string>>& listFiles)
{
	for (const pair<string, string>& pathFile : listFiles)
	{
		//处理文件名字里面包含_的  比如: CFFEX_TF_09_20190531.5m
		int index0 = pathFile.second.rfind("_");
		if (index0 != string::npos)
		{
			int index1 = pathFile.second.find(".", index0);
			if (index1 != string::npos)
			{
				int iDirYmd = atoi(pathFile.second.substr(index0 + 1, index1).c_str());
				if (iDirYmd < iYmd)
					DeleteFile(pathFile.first + "/" + pathFile.second);
			}
		}
		//处理文件名字里面不带_的  比如: 20190605.tick
		else
		{
			int index1 = pathFile.second.find(".");
			if (index1 != string::npos)
			{
				int iDirYmd = atoi(pathFile.second.substr(0, index1).c_str());
				if (iDirYmd < iYmd)
					DeleteFile(pathFile.first + "/" + pathFile.second);
			}
		}
	}
}
//删除三天之前的历史数据文件
void CleanHistoryData()
{
	auto startTime = std::chrono::steady_clock::now();
	g_Logger->info("{}->清理三天之前的本地历史数据文件", __FUNCTION__);
	CString strDigPath, strOriginalPath;
	strDigPath.Format("%s/%s", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["dig"]);
	strOriginalPath.Format("%s/%s", g_strLocalDataDir.c_str(), (LPCSTR)g_mapProfile["original"]);

	//删除三天前的数据文件
	time_t timep = ::time(NULL);
	//获取三天前的时间
	timep -= 3 * 24 * 60 * 60;
	struct tm tms = { 0 };
	//此函数获得的tm结构体的时间，是已经进行过时区转化为本地时间 
	localtime_r(&timep, &tms);
	int iYmd = (tms.tm_year + 1900) * 10000 + (tms.tm_mon + 1) * 100 + tms.tm_mday;

	g_Logger->info("开始清理dig目录: {}, 截止日期: {}", (LPCSTR)strDigPath, iYmd);
	int deletedDigFiles = 0, emptyDigDirs = 0;
	auto digStartTime = std::chrono::steady_clock::now();
	listDirAndClean(strDigPath, iYmd, deletedDigFiles, emptyDigDirs);
	auto digEndTime = std::chrono::steady_clock::now();
	auto digDuration = std::chrono::duration_cast<std::chrono::milliseconds>(digEndTime - digStartTime).count();
	g_Logger->info("dig目录清理完成: 删除文件 {}, 空目录 {}, 耗时 {}ms", deletedDigFiles, emptyDigDirs, digDuration);

	g_Logger->info("开始清理original目录: {}", (LPCSTR)strOriginalPath);
	int deletedOriginalFiles = 0, emptyOriginalDirs = 0;
	auto originalStartTime = std::chrono::steady_clock::now();
	listDirAndClean(strOriginalPath, iYmd, deletedOriginalFiles, emptyOriginalDirs);
	auto originalEndTime = std::chrono::steady_clock::now();
	auto originalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(originalEndTime - originalStartTime).count();
	g_Logger->info("original目录清理完成: 删除文件 {}, 空目录 {}, 耗时 {}ms", deletedOriginalFiles, emptyOriginalDirs, originalDuration);

	auto endTime = std::chrono::steady_clock::now();
	auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
	g_Logger->info("历史数据清理完成, 删除文件: {}, 空目录: {}, 总耗时: {}ms",
		deletedDigFiles + deletedOriginalFiles, emptyDigDirs + emptyOriginalDirs, totalDuration);
}
//----------------字符串编码--------------------//
const string encode_dict[11] = { "MA", "cQ", "cw", "Yw", "Zg", "dA", "aA", "bQ", "aw", "bw", "Ow" };
const string encode_space_dict[3] = { "MA", "cQ", "cw" };
//生成指定长度的随机字符串
string RandStr(int iLength)
{
	string strResult;
	srand(time(NULL));
	char crand[2] = { 0 };
	for (int i = 0; i < iLength; ++i)
	{
		//A-Z的ascii码范围是33-125,故此生成的随机数范围也是这个
		crand[0] = rand() % 93 + 33;
		strResult.append(crand);
	}
	return strResult;
}
string EncodeText(const string& strText)
{
	string strResult;
	//生成0..10随机数
	srand(time(NULL));
	int irand = rand() % 11;
	//map 字典
	strResult = encode_dict[irand];
	//生成随机数长度的随机字符串
	strResult += RandStr(irand);
	//字符串里面的每个字符都按位取反
	string strEncode = strText;
	for (int i = 0; i < strEncode.length(); ++i)
	{
		strEncode[i] = ~strEncode[i];
	}
	//Base64编码
	strEncode = Base64Encode(strEncode.c_str(), strEncode.size());
	//计算末尾等号的个数
	int iIndex = strEncode.find('=');
	if (iIndex != string::npos)
	{
		int iSpaceCount = strEncode.length() - iIndex;
		//去掉末尾的等号
		strEncode = strEncode.substr(0, iIndex);
		//开头添加上对应的编码字符串
		strEncode = encode_space_dict[iSpaceCount] + strEncode;
	}
	else
	{
		strEncode = encode_space_dict[0] + strEncode;
	}
	return strResult + strEncode;
}

//处理行情订阅原始数据的线程
void HandleHQDYDateProc(PVOID arg)
{
	while (true)
	{
		string strData;
		g_queueHQDYData.wait_dequeue(strData);
		//由于同时有两个客户端连接,所以要对行情进行去重
		if (IsNewTick(strData))
		{
			//解析原始行情
			OptionQuoteRsp quoteRsp;
			if (quoteRsp.ParseFromString(strData))
			{
				shared_ptr<HandledQuoteData> pHandledData = make_shared<HandledQuoteData>();
				if (ParseQuote(quoteRsp, *pHandledData))
				{
					OptionID optionId(pHandledData->strExchage, pHandledData->strComodity, pHandledData->strContractNo, pHandledData->cCallOrPutFlag, pHandledData->strStrikePrice);
					//将数据添加到队列
					g_queueHandledData.enqueue(pHandledData);
					//保存到原始行情队列
					g_lockNativeQuotes.lock();
					auto itNative = g_mapNativeQuotes.find(optionId);
					if (itNative != g_mapNativeQuotes.end())
					{
						itNative->second += pHandledData->ToQuanTick();
					}
					else
					{
						string& strNative = g_mapNativeQuotes[optionId];
						strNative += pHandledData->ToQuanTick();
					}
					g_lockNativeQuotes.unlock();
				}
			}
		}
	}
}
//计算内外盘均价和K线的线程
void HandledQuoteDataProc(PVOID arg)
{
	while (true)
	{
		shared_ptr<HandledQuoteData> pHandledData = nullptr;
		g_queueHandledData.wait_dequeue(pHandledData);
		HandleQuoteProc(pHandledData);
	}
}
void SplitString(const std::string& s, std::vector<CString>& v, const std::string& c)
{
	std::string::size_type pos1, pos2;
	pos2 = s.find(c);
	pos1 = 0;
	while (std::string::npos != pos2)
	{
		v.push_back(s.substr(pos1, pos2 - pos1));

		pos1 = pos2 + c.size();
		pos2 = s.find(c, pos1);
	}
	if (pos1 != s.length())
		v.push_back(s.substr(pos1));
}






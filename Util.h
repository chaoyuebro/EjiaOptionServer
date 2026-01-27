#pragma once

#include "DataStruct.h"
#include "HPSocket/HPSocket.h"
#include "HPSocket/TcpPullServer.h"
#include "CQuoteWriter.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include <mutex>
#include <condition_variable>
#include "BlockingQueue.h"
#include "protocol/pbOPKLineMessage.pb.h"
//与缓存服务器之间的通信协议
using namespace OptionKLineMessage;
#include "protocol/pbExchangeQuoteMessage.pb.h"
using namespace ExchangeQuoteMessage;

//条件变量,用于通知主线程 缓存初始化已经完成, 主线程开始启动服务器、监听端口并启动定时器线程等后续操作
extern std::mutex g_lockInitCache;
extern std::condition_variable g_cvInitCache;
extern CStringA s_strProtoDelimiter;
extern atomic_long lastUpdateHYXXtime;
//是否是上传数据的服务器
extern bool g_bStorageServer;
//是否是主服务器
extern volatile bool g_bIsMain;
//是否发短信
extern bool g_bSendSms;
//发短信的最短间隔
extern int g_iDXInterval;
//每天发短信的总条数
extern int g_iDXTotalNum;
//当前交易日. 为了防止DRMX数据里面出现不是当前交易日的数据
extern atomic_int g_iCurTradeDate;
//记录服务器启动时间, 为了防止收盘后向阿里云重复上传文件
extern time_t g_tServerStartTime;
//前置网关的连接id 需要推送行情
extern set<CONNID> g_setFrontEndConnids;
extern CCriSec g_lockFrontEndConnids;
//行情转发数据队列
extern BlockingQueue<string> g_queueHQDYData;
//处理过的行情数据队列
extern BlockingQueue<shared_ptr<HandledQuoteData>> g_queueHandledData;
//客户端等待缓存服务器的应答数据
extern map<UINT32, ClientKLineCache> g_mapClientKLineCache;
extern CCriSec g_lockClientKLineCache;
//当天1分钟线的时间与根数的映射,用于判断缓存数据是否需要去缓存服务器获取,降低对g_map1MinKlineDay的加锁频率
extern map<OptionID, KLineCount> g_map1MinKlineCount;
extern CCriSec g_lock1MinKlineCount;
//当天5分钟线的时间与根数的映射,用于判断缓存数据是否需要去缓存服务器获取,降低对g_map5MinKline的加锁频率
extern map<OptionID, KLineCount> g_map5MinKlineCount;
extern CCriSec g_lock5MinKlineCount;

//保存每个品种的有效交易时间,key是交易所+品种编码(不包含XYZ)比如SHFE-rb,value是多个时间区间.如果交易时间是夜盘跨天,比如21:00-2:00,则会把区间分割成两个21:00-24:00和0:00-2:00
extern map<ComodityID, list<STimeSegment>> g_mapProductTradeTime;
extern CSimpleRWLock g_lockProductTradeTime;
//合约列表
extern CStringA g_strNHeyueLiebiao;
extern CStringA g_strHeyueLiebiao;
extern CStringA g_strOPHeyueLiebiao;
extern CStringA g_strMSHeyueLiebiao;
//全部品种id下面包含的期权id列表
extern map<ContractID, list<OptionID>> g_mapOptionIDs;
extern CSimpleRWLock g_lockNHeyueLiebiao;
extern CSimpleRWLock g_lockHeyueLiebiao;
extern CSimpleRWLock g_lockOPHeyueLiebiao;
extern CSimpleRWLock g_lockMSHeyueLiebiao;
//线程池对象
extern IHPThreadPool* g_pHPThreadPool;
//原始行情列表,每5分钟往文件写一次,并清空
extern map<OptionID, string> g_mapNativeQuotes;
//已经上传过原始数据的QuoteID,防止重复上传
extern set<OptionID> g_setUploadNativeQuoteId;
extern CCriSec g_lockNativeQuotes;
//行情去重队列,长度是g_iUinqueLen,默认为5000.用list + set来实现. list主要用来实现长度控制和先进先出,set主要用来实现去重和提高查找速度
extern const int g_iUinqueLen;
extern set<CString> g_setUniqueTick;
extern list<CString> g_listUniqueTick;
//extern CCriSec g_lockUniqueTick;
//保存每一个合约的最新的计算过内外盘和均价的行情
extern map<OptionID, HandledQuoteData> g_mapLastQuote;
extern CCriSec g_lockLastQuote;
/*extern map<OptionID, std::pair<double,double>> g_mapLastSetPrice;
extern CCriSec g_lockLastSetPrice*/;
//近两百条全量数据
extern map<OptionID, list<CString>> g_map200QuanTick;
extern CCriSec g_lock200QuanTick;
//最近5分钟的增量数据
extern map<OptionID, string> g_map5MinTick;
extern CCriSec g_lock5MinTick;
//当天全部未压缩的tick增量数据,不包括当前5分钟的
extern map<OptionID, string> g_mapUnCompressTick;
//当天全部已压缩的tick增量数据,不包括当前5分钟的. value不是c风格的字符串,而是字节集
extern map<OptionID, string> g_mapCompressTick;
extern set<OptionID> g_setUploadTickQuoteId;
extern CCriSec g_lockCompressTick;
//当天1分钟线,
extern map<OptionID, KLineCache> g_map1MinKlineDay;
extern CCriSec g_lock1MinKline;
//当天5分钟线
extern map<OptionID, KLineCache> g_map5MinKline;
extern CCriSec g_lock5MinKline;
//日线
extern map<OptionID, KLine> g_mapDayKLine;
extern CCriSec g_lockDayKline;
//保存每个品种的指数的计算精度,key是交易所+品种编码(不包含XYZ)比如SHFE-rb,value是指数的计算精度,意思是保留的小数点的位数,0就是没有小数位,1就是保留一位小数
extern map<ComodityID, ComodityInfo> g_mapComodityInfo;
//期权合约信息. 登录之后从期权转发服务获取
extern CStringA g_strOPHYXX;
extern CSimpleRWLock g_lockComodityInfo;
//TradeDay.LIST. 现在改由行情服务器获取
extern string g_strTradeDayList;
extern CCriSec g_lockTradeDayList;

//读取config.properties配置文件
extern map<CString, CString> g_mapProfile;
//bOnlyIsMain参数如果为true,则只解析到IsMain配置项并且只更新g_bIsMain字段。
extern void ParseProfile(bool bOnlyIsMain = false);
//解析json内容并初始化g_mapProductTradeTime结构
void InitTradeTime();
//从阿里云oss上面获取Option/Contract/TradeDay.LIST文件并初始化服务器上面的缓存副本
extern void InitTradeDayList();
//解析json内容并初始化g_mapComodityInfo结构和期权合约信息
void InitOPHYXXAndPect(const CStringA& strJson, const bool& isUpdate = true);
//本地初始化OPHYXX
extern void InitOPHYXX();
//判断新到来的行情是否是最新的,如果是则需要推送给客户端并计算K线
extern bool IsNewTick(const CString& strTick);
//将收到的原始的行情解析成前端需要的行情格式
extern bool ParseQuote(const OptionQuoteRsp& quoteRsp, ExchangeQutoData& quoteData);
//处理新的行情,计算内外盘和均价
extern bool HandleQuote(HandledQuoteData& handledData, CString& strTick, bool bHQOld, bool& bVolume);
//GZIP压缩数据
extern int CompressData(const string& strOriginalData, string& strCompress);
//GZIP解压缩数据
extern int UnCompressData(const string& strCompress, string& strUnCompress);
//每隔5分钟重新压缩一次当天tick数据并把当前5分钟的tick数据写入文件
extern void Save5MinTick();
//每隔5分钟保存一下1m,5m和日线数据
extern void SaveKLine();
//获取给定时间所在的交易日,返回格式为yyyyMMdd
extern string GetTradeDate(time_t tTimeStamp);
//得到当前交易日,返回格式为yyyyMMdd
extern int GetTradeDateInt(time_t tTimeStamp, bool bClearCache = false);
extern int GetTradeDateInt();
//得到当前交易日,返回格式为yyyyMMdd
extern string GetTradeDate();
//提交给线程池的回调函数. 计算内外盘、均价、分钟线
extern void HandleQuoteProc(shared_ptr<HandledQuoteData> pHandledQuoteData);
//计算K线
extern void CalKLine(const ExchangeQutoData& quoteData);
//判断是否在清理缓存的时间段,只能在新交易日开始之前的19:00到19:59之间清理缓存
extern bool IsInClearCacheTime(int& iMonth);
//新交易日夜盘开始之前的19点-20点清理当天的tick、1分钟和5分钟的缓存队列。如果是当月的最后一天,则清理当月的1分钟缓存队列
extern void ClearCache();
//发送短信
extern void SendSms(const CString& strMsg);
//更新合约列表并解析ContractJson,生成全部品种id列表
void InitHeYueLieBiao(const CStringA& strData);
//在合约列表里面添加上期权的到期日. 必须在g_lockHeyueLiebiao的写锁范围内调用此函数
void UpdateHYLBExpireDate(CStringA& strHYLBJson);
//根据品种查询出精度
extern int QryJingDu(const CString& strExchange, const CString& strComodity);
//查询品种的tick
extern double QryTick(const CString& strExchange, const CString& strComodity);
//将价格按照最小变动价tick进行对齐. 四舍五入(dPrice/dTick) * dTick
extern double PriceAlignTick(double dPrice, double dTick);
//缓存恢复.五个数据结构: 最近200条全量tick, 当天所有的增量tick, 当天1分钟线, 当月1分钟线
//近200条全量tick是从当天所有的增量tick里面计算恢复的. bOnlyM1Month为true时,仅恢复当月1分钟K线
extern void RecoverCache(bool bOnlyM1Month = false);
//如果已经收盘，就计算主力合约并上传
extern void UploadDataToAliOSS();
//删除三天之前的历史数据文件
extern void CleanHistoryData();
//边扫描边删除的优化版本
extern void listDirAndClean(const string& strPath, int iYmd, int& deletedFiles, int& emptyDirs);
//生成长度在10-16之间的随机字符串
extern string RandStr(int iLength);
//对字符串进行编码
extern string EncodeText(const string& strText);
//对字符串进行解码
extern string DecodeText(const string& strRule);
//处理行情订阅原始数据的线程
extern void HandleHQDYDateProc(PVOID arg);
//计算内外盘均价和K线的线程
extern void HandledQuoteDataProc(PVOID arg);
extern void SplitString(const std::string& s, std::vector<CString>& v, const std::string& c);
//将郑商所的合约代码从3位修改为4位
extern string ModifyZCEContractNo(const string& strContractNo);
extern void UpdateNHYLBExpireDate(CStringA&);
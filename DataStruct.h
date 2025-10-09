#pragma once
#include "HPSocket/common/GlobalDef.h"
#include "HPSocket/global/helper.h"
#include <string>
#include <thread>
#include <list>
#include <set>
#include <chrono>
using namespace std;

//时间区间
struct STimeSegment
{
	STimeSegment(int from, int to)
		:From(from), To(to)
	{};
	int From;
	int To;
};
//根据品种查询出精度和Tick
extern int QryJingDu(const CString& strExchange, const CString& strComodity);
//查询品种的tick
extern double QryTick(const CString& strExchange, const CString& strComodity);
//将价格按照最小变动价tick进行对齐. 四舍五入(dPrice/dTick) * dTick
extern double PriceAlignTick(double dPrice, double dTick);
//从行情转发服务收到的行情
struct ExchangeQutoData
{
	ExchangeQutoData()
		:tTimeStamp(0), QPrePositionQty(0), QTotalQty(0), QPositionQty(0), QLastQty(0)
	{}
	//交易日
	CString strTradeDate;
	//交易所
	CString strExchage;
	//品种
	CString strComodity;
	//合约代码
	CString strContractNo;
	//看涨看跌标示
	char cCallOrPutFlag;
	//行权价格
	CString strStrikePrice;
	//时间24时制,日期和时间类型(格式 yyyy-MM-dd hh:nn:ss) 
	CString strDateTime;
	//时间戳
	time_t tTimeStamp;
	//毫秒数
	CString strMilisec;
	//昨收盘价
	double QPreClosingPrice;
	//昨结算价
	double QPreSettlePrice;
	//昨持仓量
	INT64 QPrePositionQty;
	//开盘价
	double QOpeningPrice;
	//最新价
	double QLastPrice;
	//最高价
	double QHighPrice;
	//最低价
	double QLowPrice;
	//涨停价
	double QLimitUpPrice;
	//跌停价
	double QLimitDownPrice;
	//当日总成交量
	INT64 QTotalQty;
	//持仓量
	INT64 QPositionQty;
	//收盘价
	double QClosingPrice;
	//结算价
	double QSettlePrice;
	//最新成交量，现手
	INT64 QLastQty;
	//买价1-5档
	double QBidPrice[5];
	//买量1-5档
	INT64 QBidQty[5];
	//卖价1-5档
	double QAskPrice[5];
	//卖量1-5档
	INT64 QAskQty[5];
};
//计算过内外盘和均价的行情
struct HandledQuoteData : public ExchangeQutoData
{
	HandledQuoteData()
		:ExchangeQutoData(), iNeiPan(0), iWaiPan(0), dAvgPrice(0.0)
	{}
	//内盘
	int iNeiPan;
	//外盘
	int iWaiPan;
	//均价
	double dAvgPrice;
	//如果是指数,就返回空字符串.因为指数不计算内外盘
	CString GetNeiPan() const
	{
		CString strRes;
		if (strContractNo.CompareNoCase("IDX") != 0)
			strRes.Format("%d", iNeiPan);
		return strRes;
	}
	//如果是指数,就返回空字符串.因为指数不计算内外盘
	CString GetWaiPan() const
	{
		CString strRes;
		if (strContractNo.CompareNoCase("IDX") != 0)
			strRes.Format("%d", iWaiPan);
		return strRes;
	}
	//按照该品种的精度将均价输出为字符串
	CString GetAvgPrice() const
	{
		int pect = QryJingDu(strExchage, strComodity);
		double dTick = QryTick(strExchage, strComodity);
		CString strFormat;
		//第一个%是转义字符, 如果pect是3,则生成的字符串就是 %.3f
		strFormat.Format("%%.%df", pect);
		CString strAvgPrice;
		strAvgPrice.Format(strFormat, PriceAlignTick(dAvgPrice, dTick));
		return strAvgPrice;
	}
	//生成全量的tick行情字符串
	CString ToQuanTick() const
	{
		int pect = QryJingDu(strExchage, strComodity);
		CString strFormat;
		//第一个%是转义字符, 如果pect是3,则生成的字符串就是 %.3f
		strFormat.Format("%%.%df", pect);
		CString strTick;
		strTick.Format("%s|%s|%s|%s|%c|%s|%lld|%s", (LPCSTR)strTradeDate, (LPCSTR)strExchage, (LPCSTR)strComodity, (LPCSTR)strContractNo, cCallOrPutFlag, (LPCSTR)strStrikePrice, tTimeStamp, (LPCSTR)strMilisec);
		strTick.Append("|");
		CString strTemp;
		strTemp.Format(strFormat, QPreClosingPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QPreSettlePrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format("%lld", QPrePositionQty);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QOpeningPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QLastPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QHighPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QLowPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QLimitUpPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QLimitDownPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format("%lld", QTotalQty);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format("%lld", QPositionQty);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QClosingPrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, QSettlePrice);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format("%lld", QLastQty);
		strTick.Append(strTemp);
		strTick.Append("|");
		for (int i = 0; i < 5; ++i)
		{
			strTemp.Format(strFormat, QBidPrice[i]);
			strTick.Append(strTemp);
			strTick.Append("|");
		}
		for (int i = 0; i < 5; ++i)
		{
			strTemp.Format("%lld", QBidQty[i]);
			strTick.Append(strTemp);
			strTick.Append("|");
		}
		for (int i = 0; i < 5; ++i)
		{
			strTemp.Format(strFormat, QAskPrice[i]);
			strTick.Append(strTemp);
			strTick.Append("|");
		}
		for (int i = 0; i < 5; ++i)
		{
			strTemp.Format("%lld", QAskQty[i]);
			strTick.Append(strTemp);
			strTick.Append("|");
		}
		strTemp.Format("%lld", iNeiPan);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format("%lld", iWaiPan);
		strTick.Append(strTemp);
		strTick.Append("|");
		strTemp.Format(strFormat, dAvgPrice);
		strTick.Append(strTemp);
		strTick.Append("\n");
		return strTick;
	}
	//生成增量字符串
	CString ToZengTick(const HandledQuoteData& data)
	{
		int pect = QryJingDu(strExchage, strComodity);
		CString strFormat;
		//第一个%是转义字符, 如果pect是3,则生成的字符串就是 %.3f
		strFormat.Format("%%.%df", pect);
		CString strTick;
		strTick.Format("%s|%s|%s|%s|%c|%s|%lld|%s", (LPCSTR)strTradeDate, (LPCSTR)strExchage, (LPCSTR)strComodity, (LPCSTR)strContractNo, cCallOrPutFlag, (LPCSTR)strStrikePrice, tTimeStamp, (LPCSTR)strMilisec);
		strTick.Append("|");
		CString strTemp;
		if (QPreClosingPrice != data.QPreClosingPrice)
		{
			strTemp.Format(strFormat, QPreClosingPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QPreSettlePrice != data.QPreSettlePrice)
		{
			strTemp.Format(strFormat, QPreSettlePrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QPrePositionQty != data.QPrePositionQty)
			strTick.AppendFormat("%lld", QPrePositionQty);
		strTick.Append("|");
		if (QOpeningPrice != data.QOpeningPrice)
		{
			strTemp.Format(strFormat, QOpeningPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QLastPrice != data.QLastPrice)
		{
			strTemp.Format(strFormat, QLastPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QHighPrice != data.QHighPrice)
		{
			strTemp.Format(strFormat, QHighPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QLowPrice != data.QLowPrice)
		{
			strTemp.Format(strFormat, QLowPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QLimitUpPrice != data.QLimitUpPrice)
		{
			strTemp.Format(strFormat, QLimitUpPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QLimitDownPrice != data.QLimitDownPrice)
		{
			strTemp.Format(strFormat, QLimitDownPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QTotalQty != data.QTotalQty)
			strTick.AppendFormat("%lld", QTotalQty);
		strTick.Append("|");
		if (QPositionQty != data.QPositionQty)
			strTick.AppendFormat("%lld", QPositionQty);
		strTick.Append("|");
		if (QClosingPrice != data.QClosingPrice)
		{
			strTemp.Format(strFormat, QClosingPrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QSettlePrice != data.QSettlePrice)
		{
			strTemp.Format(strFormat, QSettlePrice);
			strTick.Append(strTemp);
		}
		strTick.Append("|");
		if (QLastQty != data.QLastQty)
			strTick.AppendFormat("%lld", QLastQty);
		for (int i = 0; i < 5; ++i)
		{
			strTick.Append("|");
			if (QBidPrice[i] != data.QBidPrice[i])
			{
				strTemp.Format(strFormat, QBidPrice[i]);
				strTick.Append(strTemp);
			}
		}
		for (int i = 0; i < 5; ++i)
		{
			strTick.Append("|");
			if (QBidQty[i] != data.QBidQty[i])
			{
				strTick.AppendFormat("%lld", QBidQty[i]);
			}
		}
		for (int i = 0; i < 5; ++i)
		{
			strTick.Append("|");
			if (QAskPrice[i] != data.QAskPrice[i])
			{
				strTemp.Format(strFormat, QAskPrice[i]);
				strTick.Append(strTemp);
			}
		}
		for (int i = 0; i < 5; ++i)
		{
			strTick.Append("|");
			if (QAskQty[i] != data.QAskQty[i])
			{
				strTick.AppendFormat("%lld", QAskQty[i]);
			}
		}
		strTick.AppendFormat("|%s|%s|%s\n", (LPCSTR)GetNeiPan(), (LPCSTR)GetWaiPan(), (LPCSTR)GetAvgPrice());
		return strTick;
	}
};
//品种信息
struct ComodityInfo
{
	ComodityInfo(int pect, double tick)
		:iPect(pect), dTick(tick)
	{}
	ComodityInfo()
		:iPect(0), dTick(0.0)
	{}
	//精度。计算指数时保留的小数点位数
	int iPect;
	//最小变动价位.计算指数时,要按照这个最小变动价位做四舍五入
	double dTick;
};
enum ClientType
{
	UNKNOWN,
	PC,
	ANDROID,
	IOS,
	WEB
};
//客户端信息
struct ClientInfo
{
	ClientInfo(LPCSTR lpClientIp = "")
		: tConnectTime(::time(NULL)), bIsPassed(false), clientType(UNKNOWN), strIP(lpClientIp)
	{}
	static ClientType ParseClientType(char cType)
	{
		ClientType res = UNKNOWN;
		switch (cType)
		{
		case '1':
		{
			res = PC;
			break;
		}
		case '2':
		{
			res = ANDROID;
			break;
		}
		case '3':
		{
			res = IOS;
			break;
		}
		case '4':
		{
			res = WEB;
			break;
		}
		default:
		{
			break;
		}
		}
		return res;
	}
	//客户端的连接时间
	time_t tConnectTime;
	//服务端下发的随机字符串,用于鉴权
	CString strRandomKey;
	//鉴权是否通过
	bool bIsPassed;
	//客户端类型
	ClientType clientType;
	//客户端的ip地址
	CString strIP;
};
//品种ID
struct ComodityID
{
	ComodityID()
	{}
	ComodityID(LPCSTR exchange, LPCSTR comodity)
		:strExchage(exchange), strComodity(comodity)
	{}
	//交易所
	CString strExchage;
	//品种
	CString strComodity;
	bool operator <(const ComodityID& d) const
	{
		if (this->strExchage.CompareNoCase(d.strExchage) == 0)
			return this->strComodity.CompareNoCase(d.strComodity) < 0;
		else
			return this->strExchage.CompareNoCase(d.strExchage) < 0;
	}
	bool operator ==(const ComodityID& d) const
	{
		return this->strExchage.CompareNoCase(d.strExchage) == 0 &&
			this->strComodity.CompareNoCase(d.strComodity) == 0;
	}
};
//合约ID
struct ContractID : public ComodityID
{
	ContractID()
	{}
	ContractID(LPCSTR exchange, LPCSTR comodity, LPCSTR contractNo)
		: ComodityID(exchange, comodity), strContractNo(contractNo)
	{}
	//合约代码.
	CString strContractNo;
	bool operator <(const ContractID& d) const
	{
		if (this->strContractNo.CompareNoCase(d.strContractNo) == 0)
			return ComodityID::operator<(d);
		else
			return this->strContractNo.CompareNoCase(d.strContractNo) < 0;
	}
	bool operator ==(const ContractID& d) const
	{
		return ComodityID::operator==(d) &&
			this->strContractNo.CompareNoCase(d.strContractNo) == 0;
	}
	CString ToString() const
	{
		CString strRes;
		strRes.Format("%s|%s|%s\n", strExchage.c_str(), strComodity.c_str(), strContractNo.c_str());
		return strRes;
	}
};
//期权合约ID
struct OptionID : public ContractID
{
	OptionID()
	{}
	OptionID(LPCSTR exchange, LPCSTR comodity, LPCSTR contractNo, char callOrPutFlag, LPCSTR strikePrice)
		: ContractID(exchange, comodity, contractNo), cCallOrPutFlag(callOrPutFlag), strStrikePrice(strikePrice)
	{}
	char cCallOrPutFlag;
	CString strStrikePrice;
	bool operator <(const OptionID& d) const
	{
		return this->ToString().CompareNoCase(d.ToString()) < 0;
		//if (this->strContractNo.CompareNoCase(d.strContractNo) == 0)
		//	return ComodityID::operator<(d);
		//else
		//	return this->strContractNo.CompareNoCase(d.strContractNo) < 0;
	}
	bool operator ==(const OptionID& d) const
	{
		return ComodityID::operator==(d) &&
			this->strContractNo.CompareNoCase(d.strContractNo) == 0 &&
			this->cCallOrPutFlag == d.cCallOrPutFlag &&
			this->strStrikePrice.CompareNoCase(d.strStrikePrice) == 0;
	}
	CString ToString() const
	{
		CString strRes;
		strRes.Format("%s|%s|%s|%c|%s\n", strExchage.c_str(), strComodity.c_str(), strContractNo.c_str(), cCallOrPutFlag, strStrikePrice.c_str());
		return strRes;
	}
};
//K线
struct KLine
{
	KLine()
		:iTimeStamp(0), i64Volume(0), i64TotalVolume(0), i64PositionQty(0)
	{}
	KLine(const CString& tradeDate, UINT64 timestamp, double open, double high, double low, double close, const CString& volume, const CString& position, int pect)
		: strTradeDate(tradeDate), iTimeStamp(timestamp), dOpen(open), dHigh(high), dLow(low), dClose(close), i64TotalVolume(0), iPect(pect)
	{
		i64Volume = atoll(volume.c_str());
		i64PositionQty = atoll(position.c_str());
	}
	CString strTradeDate;
	//UINT32的表示范围是0~4294967295。 当前时间戳是1568104558 够用了
	UINT64 iTimeStamp;
	double dOpen;
	double dHigh;
	double dLow;
	double dClose;
	//当前时间内的成交量
	INT64 i64Volume;
	//当日总成交量
	INT64 i64TotalVolume;
	//持仓量
	INT64 i64PositionQty;
	int iPect;

	bool IsValid()
	{
		return !strTradeDate.IsEmpty();
	}
	CString ToString()
	{
		CString strFormat;
		//第一个%是转义字符, 如果pect是3,则生成的字符串就是 %.3f
		strFormat.Format("%%.%df", iPect);
		CString strKLine;
		strKLine.Format("%s|%llu|", (LPCSTR)strTradeDate, iTimeStamp);
		CString strTemp;
		strTemp.Format(strFormat, dOpen);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dHigh);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dLow);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dClose);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strKLine.AppendFormat("%lld|%lld\n", i64Volume, i64PositionQty);
		return strKLine;
	}
	//日线字符串里面没有时间戳
	CString ToDayString()
	{
		CString strFormat;
		//第一个%是转义字符, 如果pect是3,则生成的字符串就是 %.3f
		strFormat.Format("%%.%df", iPect);
		CString strKLine;
		strKLine.Format("%s|", (LPCSTR)strTradeDate);
		CString strTemp;
		strTemp.Format(strFormat, dOpen);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dHigh);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dLow);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strTemp.Format(strFormat, dClose);
		strKLine.Append(strTemp);
		strKLine.Append("|");
		strKLine.AppendFormat("%lld|%lld\n", i64Volume, i64PositionQty);
		return strKLine;
	}
};
//K线缓存
struct KLineCache
{
	//已走完的K线缓存
	map<UINT32, KLine, greater<UINT32>> mapCache;
	//最新一根未走完的K线
	KLine lastKLine;
	string ToString()
	{
		string strResult;
		//因为mapCache是按照时间倒序排序的,而返回的结果里面是按照时间升序排序,所以遍历时应该倒序遍历
		for (auto itMap = mapCache.rbegin(); itMap != mapCache.rend(); ++itMap)
			strResult += itMap->second.ToString();
		if (lastKLine.IsValid())
			strResult += lastKLine.ToString();
		return strResult;
	}
	//根据根数和结束时间戳取K线
	void GetCountKLines(UINT32 iCount, UINT32 iEndTimeStamp, list<KLine>& listResult)
	{
		int count = 1;
		if (lastKLine.IsValid() && lastKLine.iTimeStamp <= iEndTimeStamp)
		{
			++count;
			listResult.push_front(lastKLine);
		}
		//itM1Cache->second是按照key降序排序的,lower_bound返回的是小于等于给定值的第一个元素的地址,upper_bound返回的是小于给定值的第一个元素的地址
		auto it = mapCache.lower_bound(iEndTimeStamp);
		//从it开始往前取iTotalCount根K线
		for (; it != mapCache.end() && count <= iCount; ++it, ++count)
		{
			//升序排序
			listResult.push_front(it->second);
		}
	}
	//根据起止时间取K线
	void GetTimeKLines(UINT32 iStartTimeStamp, UINT32 iEndTimeStamp, list<KLine>& listResult)
	{
		if (lastKLine.IsValid() && lastKLine.iTimeStamp <= iEndTimeStamp && lastKLine.iTimeStamp >= iStartTimeStamp)
			listResult.push_front(lastKLine);
		//itM1Cache->second是按照key降序排序的,lower_bound返回的是小于等于给定值的第一个元素的地址,upper_bound返回的是小于给定值的第一个元素的地址
		auto itbegin = mapCache.lower_bound(iEndTimeStamp);
		if (itbegin != mapCache.end())
		{
			auto itend = mapCache.upper_bound(iStartTimeStamp);
			//取itbegin与itend之间的K线
			for (auto it = itbegin; it != itend; ++it)
				listResult.push_front(it->second);
		}
	}
	//根据结束时间取K线,获取当天第一根到结束时间之间的K线.主要用于补充当天的数据
	void GetTimeKLines(UINT32 iEndTimeStamp, list<KLine>& listResult)
	{
		if (lastKLine.IsValid() && lastKLine.iTimeStamp <= iEndTimeStamp)
			listResult.push_front(lastKLine);
		//itM1Cache->second是按照key降序排序的,lower_bound返回的是小于等于给定值的第一个元素的地址,upper_bound返回的是小于给定值的第一个元素的地址
		auto itbegin = mapCache.lower_bound(iEndTimeStamp);
		if (itbegin != mapCache.end())
		{
			//取itbegin与end()之间的K线
			for (auto it = itbegin; it != mapCache.end(); ++it)
				listResult.push_front(it->second);
		}
	}
	//根据给定的开始时间判断是否需要去缓存服务器读取更多数据, 如果需要则iEndTimeStamp返回的是新的结束时间戳
	bool NeedMoreCache(UINT32 iStartTimeStamp, UINT32& iEndTimeStamp)
	{
		bool bResult = true;
		if (mapCache.size() > 0)
		{
			if (mapCache.rbegin()->second.iTimeStamp <= iStartTimeStamp)
				bResult = false;
			else
				iEndTimeStamp = mapCache.rbegin()->second.iTimeStamp - 1;
		}
		else
		{
			if (lastKLine.IsValid())
			{
				if (lastKLine.iTimeStamp <= iStartTimeStamp)
					bResult = false;
				else
					iEndTimeStamp = lastKLine.iTimeStamp - 1;
			}
		}
		return bResult;
	}
};
//当天K线的时间点和对应的根数
struct KLineCount
{
	//已走完的K线的时间戳与根数的映射,key是K线的时间戳,value是当天第一根K线到当根K线之间的根数
	map<UINT32, UINT32, greater<UINT32>> mapTimeCount;
};
//K线周期
enum KLineType
{
	KM1 = 0,					//1分钟
	KM5 = 1,					//5分钟
	KDAY = 2					//日线
};
enum ProtocolType
{
	CCACHE = 0,
	TCACHE = 1,
};
//客户端等待缓存服务器的应答数据
struct ClientKLineCache
{
	ClientKLineCache()
		:iClientReqId(0), dwConnId(0), klineType(KLineType::KDAY), protocolType(ProtocolType::CCACHE), iEndTimeStamp(0), bNeedNative(false)
	{}
	//ClientKLineCache(QuoteID quoteid, UINT32 reqid, ULONG connid, KLineType klinetype, ProtocolType type, UINT32 endtime, bool neednative)
	//	:quoteId(quoteid), iClientReqId(reqid), dwConnId(connid), klineType(klinetype), protocolType(type), iEndTimeStamp(endtime), bNeedNative(neednative)
	//{}
	ClientKLineCache(OptionID quoteid, UINT32 frontReqId, UINT32 reqid, ULONG connid, KLineType klinetype, ProtocolType type, UINT32 endtime, bool neednative, const chrono::steady_clock::time_point& tStart)
		:optionId(quoteid), iFrontEndReqId(frontReqId), iClientReqId(reqid), dwConnId(connid), klineType(klinetype), protocolType(type), iEndTimeStamp(endtime), bNeedNative(neednative), tCacheReq(tStart)
	{}
	OptionID optionId;
	//FrontEnd转发的ReqId. 填在front_packet_t的包头里面
	UINT32 iFrontEndReqId;
	//客户端传过来的请求id. 要原封不动的写回到pb数据结构里面
	UINT32 iClientReqId;
	//客户端的连接id
	ULONG dwConnId;
	//K线周期
	KLineType klineType;
	//给客户端返回的协议类型
	ProtocolType protocolType;
	//需要获取当天K线数据的结束时间戳
	UINT32 iEndTimeStamp;
	//是否需要补充本地数据
	bool bNeedNative;
	//向缓存服务器发送请求的时间点
	chrono::steady_clock::time_point tCacheReq;
};


#include "WorkerProcessor.h"
#include "Util.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <sstream>
#include "./protocol/pbOPKLineMessage.pb.h"
#include "TcpCacheClient.h"
using namespace OptionKLineMessage;
//缓存客户端
extern CTcpCacheClient* g_pCacheClient;
//登录时正确的密码
string WorkerProcessor::s_strRightPwd = "FL@G4AES8LJF3R3RT5POE9KFA@HFASF$;NASF(*&J;JFEASFJKJ;FEAF^FEAFHLL";
//判断数据包是否完整，如果完整返回完整数据包的长度，否则返回-1
int WorkerProcessor::get_length(const BYTE* bData, int nDataLen)
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
//业务处理。根据包头命令，执行相应操作
void WorkerProcessor::Parse(const BYTE* bData, int iDataLen, ULONG dwConnId, string& resultBuffer, bool& bForceDisconnect)
{
	bForceDisconnect = false;
	front_packet_t* frontPacket = (front_packet_t*)bData;
	data_packet_t* packet = (data_packet_t*)frontPacket->data;
	CStringA strProtocol(packet->protocl, sizeof(packet->protocl));
	strProtocol.Trim();
	//g_Logger->info("get strProtocol:{},size{}", strProtocol, sizeof(packet->protocl));
	UINT32 iDataSize = ntohl(frontPacket->size);
	int iDataLength = iDataSize - sizeof(packet->protocl) - sizeof(packet->detailType) - s_strProtoDelimiter.GetLength();
	if (iDataLength >= 0)
	{
		char detailType[4] = { ' ' };
		if (strProtocol.CompareNoCase("LGIN") == 0)
		{
			CStringA strData(packet->data, iDataLength);
			if (strData.compare(s_strRightPwd) == 0)
			{
				g_Logger->info("{}->{}登录成功", __FUNCTION__, dwConnId);
				//保存前置网关服务的连接
				{
					CCriSecLock locallock(g_lockFrontEndConnids);
					g_setFrontEndConnids.insert(dwConnId);
				}
				//给前置网关服务推送每个合约最新的行情
				{
					CCriSecLock locallock(g_lockLastQuote);
					for (auto pair : g_mapLastQuote)
					{
						CString strTick = pair.second.ToQuanTick();
						//推送行情的时候，行情全文前面添加上OptionID
						ophq_packet_t hq_pkt;
						::memset(&hq_pkt, 0, sizeof(ophq_packet_t));
						memcpy(hq_pkt.exchange, pair.second.strExchage.c_str(), pair.second.strExchage.size());
						memcpy(hq_pkt.comodity, pair.second.strComodity.c_str(), pair.second.strComodity.size());
						memcpy(hq_pkt.contractNo, pair.second.strContractNo.c_str(), pair.second.strContractNo.size());
						hq_pkt.callOrPutFlag = pair.second.cCallOrPutFlag;
						memcpy(hq_pkt.strikePrice, pair.second.strStrikePrice.c_str(), pair.second.strStrikePrice.size());
						//拼接包头
						front_packet_t packet;
						packet.cmd = cmd_OPHQ;
						packet.reqid = 0;
						packet.size = htonl(sizeof(ophq_packet_t) + strTick.size());
						resultBuffer.append(string((LPCSTR)&packet, sizeof(packet)));
						resultBuffer.append((LPCSTR)&hq_pkt, sizeof(ophq_packet_t));
						resultBuffer.append(strTick);
					}
				}
			}
			else
			{
				g_Logger->error("{}->{}登录错误:{}", __FUNCTION__, dwConnId, (LPCSTR)strData);
				bForceDisconnect = true;
			}
		}
		//期权合约信息
		else if (strProtocol.CompareNoCase("OPHYXX") == 0)
		{
			string strResData;
			CReadLock readlock(g_lockComodityInfo);
			memcpy(detailType, "TXT", sizeof(detailType));
			strResData.append(packet->protocl, sizeof(packet->protocl));
			strResData.append(detailType, sizeof(detailType));
			strResData.append(g_strOPHYXX);
			strResData.append(s_strProtoDelimiter);
			//加包头
			ADD_PACKET_HEAD
		}
		else if (strProtocol.CompareNoCase("OPTDST") == 0)
		{
			string strResData;
			memcpy(detailType, "GZIP", sizeof(detailType));
			strResData.append(packet->protocl, sizeof(packet->protocl));
			strResData.append(detailType, sizeof(detailType));
			g_lockTradeDayList.lock();
			strResData.append(g_strTradeDayList);
			g_lockTradeDayList.unlock();
			strResData.append(s_strProtoDelimiter);
			//加包头
			ADD_PACKET_HEAD
		}
		//合约列表
		else if (strProtocol.CompareNoCase("OPHYLB") == 0)
		{
			string strResData;
			CReadLock readlock(g_lockOPHeyueLiebiao);
			memcpy(detailType, "TXT", sizeof(detailType));
			strResData.append(packet->protocl, sizeof(packet->protocl));
			strResData.append(detailType, sizeof(detailType));
			strResData.append(g_strOPHeyueLiebiao);
			//g_Logger->info("add g_strOPHeyueLiebiao->{}", g_strOPHeyueLiebiao);
			readlock.unlock();
			strResData.append(s_strProtoDelimiter);
			//加包头
			ADD_PACKET_HEAD
		}
		else if (strProtocol.CompareNoCase("MSHYLB") == 0)
		{
			string strResData;
			CReadLock readlock(g_lockMSHeyueLiebiao);
			memcpy(detailType, "TXT", sizeof(detailType));
			strResData.append(packet->protocl, sizeof(packet->protocl));
			strResData.append(detailType, sizeof(detailType));
			strResData.append(g_strMSHeyueLiebiao);
			readlock.unlock();
			strResData.append(s_strProtoDelimiter);
			//加包头
			ADD_PACKET_HEAD
		}
		//当日明细
		else if (strProtocol.CompareNoCase("OPDRMX") == 0)
		{
			CStringA strData(packet->data, iDataLength);
			//一次可能会请求多个合约,协议体内容:ZCE|CF|905|C|3500|N\n SHFE|BU|2009|C|3500|N\n  N是可选项,如果有N就返回N条全量tick,如果没有就返回当天所有的增量tick		
			vector<CString> vecData;
			SplitStr(strData, vecData, "\n");
			for (CString strReq : vecData)
			{
				if (strReq.IsEmpty())
					continue;
				vector<CString> vecDRData;
				SplitStr(strReq, vecDRData, "|");
				if (vecDRData.size() == 5 || vecDRData.size() == 6)
				{
					CString strExchange, strComodity, strContractNo, strStrikePrice;
					strExchange = vecDRData[0].Trim();
					strComodity = vecDRData[1].Trim().MakeUpper();
					strContractNo = vecDRData[2].Trim();
					char cCallOrPutFlag = vecDRData[3][0];
					strStrikePrice = vecDRData[4];
					OptionID optionId(strExchange, strComodity, strContractNo, cCallOrPutFlag, strStrikePrice);
					//取当天所有的增量tick
					if (vecDRData.size() == 5)
					{
						string strResult, strOriginal;
						//先拼接已压缩的数据
						g_lockCompressTick.lock();
						auto itCompress = g_mapCompressTick.find(optionId);
						if (itCompress != g_mapCompressTick.end())
							strResult = itCompress->second;
						g_lockCompressTick.unlock();
						//如果没有已压缩的数据,则只压缩 交易所|品种|合约\n
						if (strResult.empty())
						{
							ostringstream ssResult;
							ssResult << optionId.ToString();
							string strCompress;
							int iRet = CompressData(ssResult.str(), strCompress);
							strResult = strCompress;
						}
						//中间用特殊的分割符
						strResult += "@#$%";
						//再拼接未压缩的数据
						g_lock5MinTick.lock();
						auto itUnCompress = g_map5MinTick.find(optionId);
						if (itUnCompress != g_map5MinTick.end())
							strOriginal += itUnCompress->second;
						g_lock5MinTick.unlock();
						if (strOriginal.size() > 0)
						{
							string strCompress;
							int iRet = CompressData(strOriginal, strCompress);
							if (0 == iRet)
								strResult += strCompress;
						}
						string strResData;
						memcpy(detailType, "ZIP2", sizeof(detailType));
						strResData.append(packet->protocl, sizeof(packet->protocl));
						strResData.append(detailType, sizeof(detailType));
						strResData.append(strResult);
						strResData.append(s_strProtoDelimiter);
						//加包头
						ADD_PACKET_HEAD
					}
					//取N条全量tick
					else if (vecDRData.size() == 6)
					{
						int N = atoi(vecDRData[5].Trim());
						if (N > 0)
						{
							ostringstream ssOriginalData;
							ssOriginalData << vecDRData[0] << "|" << vecDRData[1] << "|" << vecDRData[2] << "|" << vecDRData[3] << "|" << vecDRData[4] << "|" << vecDRData[5] << "\n";
							g_lock200QuanTick.lock();
							auto it200 = g_map200QuanTick.find(optionId);
							if (it200 != g_map200QuanTick.end())
							{
								if (N > it200->second.size())
									N = it200->second.size();
								auto itBegin = it200->second.begin();
								advance(itBegin, it200->second.size() - N);
								for (auto it = itBegin; it != it200->second.end(); ++it)
									ssOriginalData << *it;
							}
							g_lock200QuanTick.unlock();
							//压缩
							string strCompress;
							int iRet = CompressData(ssOriginalData.str(), strCompress);
							string strResData;
							memcpy(detailType, "GZIP", sizeof(detailType));
							strResData.append(packet->protocl, sizeof(packet->protocl));
							strResData.append(detailType, sizeof(detailType));
							strResData.append(strCompress);
							strResData.append(s_strProtoDelimiter);
							//加包头
							ADD_PACKET_HEAD
						}
					}
				}
				else
					g_Logger->error("{}->{}请求数据解析错误:{}", __FUNCTION__, (LPCSTR)strProtocol, (LPCSTR)strReq);
			}
		}
		//当日1分钟
		else if (strProtocol.CompareNoCase("OPDR1K") == 0)
		{
			CStringA strData(packet->data, iDataLength);
			//一次只会请求一个合约,协议体内容样例: ZCE|CF|805|C|3500			
			vector<CString> vecReqData;
			SplitStr(strData, vecReqData, "|");
			if (vecReqData.size() == 5)
			{
				CString strExchange, strComodity, strContractNo, strStrikePrice;
				strExchange = vecReqData[0].Trim();
				strComodity = vecReqData[1].Trim().MakeUpper();
				strContractNo = vecReqData[2].Trim();
				char cCallOrPutFlag = vecReqData[3][0];
				strStrikePrice = vecReqData[4];
				OptionID optionId(strExchange, strComodity, strContractNo, cCallOrPutFlag, strStrikePrice);
				//获取当天1分钟K线的全量数据
				string strOriginalData(strData + "\n");
				g_lock1MinKline.lock();
				auto it1m = g_map1MinKlineDay.find(optionId);
				if (it1m != g_map1MinKlineDay.end())
					strOriginalData += it1m->second.ToString();
				g_lock1MinKline.unlock();
				//压缩并返回
				string strCompress;
				int iRet = CompressData(strOriginalData, strCompress);
				string strResData;
				::memcpy(detailType, "GZIP", sizeof(detailType));
				strResData.append(packet->protocl, sizeof(packet->protocl));
				strResData.append(detailType, sizeof(detailType));
				strResData.append(strCompress);
				strResData.append(s_strProtoDelimiter);
				//加包头
				ADD_PACKET_HEAD
			}
			else
				g_Logger->error("{}->{}请求解析错误:{}", __FUNCTION__, (LPCSTR)strProtocol, (LPCSTR)strData);
		}
		//按照截止时间戳,向前取给定根数的K线缓存
		else if (strProtocol.CompareNoCase("OPCCACHE") == 0)
		{
			auto t1 = std::chrono::steady_clock::now();
			OPKLineCountRequest cklineCountReq;
			if (cklineCountReq.ParseFromArray(packet->data, iDataLength))
			{
				auto pbOptionid = cklineCountReq.optionid();
				OptionID quoteId(pbOptionid.strexchange().c_str(), pbOptionid.strcomodityxyz().c_str(), pbOptionid.strcontractno().c_str(), pbOptionid.strcallorputflag()[0], pbOptionid.strstrikeprice().c_str());
				UINT32 iTotalCount = cklineCountReq.icount();
				UINT32 iEndTimeStamp = cklineCountReq.iendtimestamp();
				pbOPKLineType klineType = cklineCountReq.type();
				KLineType cacheKlineType;
				bool bNeedMoreCache = true;
				list<KLine> listResult;
				UINT32 iCacheCount = iTotalCount;
				UINT32 iCacheEndTime = iEndTimeStamp;
				if (iTotalCount > 0)
				{
					switch (klineType)
					{
					case pbOPKLineType::M1:
					{
						cacheKlineType = KLineType::KM1;
						g_lock1MinKlineCount.lock();
						auto it1m = g_map1MinKlineCount.find(quoteId);
						if (it1m != g_map1MinKlineCount.end())
						{
							auto it = it1m->second.mapTimeCount.lower_bound(iEndTimeStamp);
							if (it != it1m->second.mapTimeCount.end())
							{
								//超过了需要的根数,则不需要去获取缓存
								if (it->second >= iTotalCount)
									bNeedMoreCache = false;
								else
								{
									//根数不够,需要去缓存服务器取数据. 取的根数就是总数与当前根数的差值, 截止时间就是当天第一根K线的时间戳-1
									iCacheCount = iTotalCount - it->second;
									iCacheEndTime = it1m->second.mapTimeCount.rbegin()->first - 1;
								}
							}
						}
						g_lock1MinKlineCount.unlock();
						//不需要更多缓存,则直接从本地缓存取,然后直接返回给客户端
						if (!bNeedMoreCache)
						{
							g_lock1MinKline.lock();
							auto it1m = g_map1MinKlineDay.find(quoteId);
							if (it1m != g_map1MinKlineDay.end())
							{
								it1m->second.GetCountKLines(iTotalCount, iEndTimeStamp, listResult);
							}
							g_lock1MinKline.unlock();
						}
						break;
					}
					case pbOPKLineType::M5:
					{
						cacheKlineType = KLineType::KM5;
						g_lock5MinKlineCount.lock();
						auto it5m = g_map5MinKlineCount.find(quoteId);
						if (it5m != g_map5MinKlineCount.end())
						{
							auto it = it5m->second.mapTimeCount.lower_bound(iEndTimeStamp);
							if (it != it5m->second.mapTimeCount.end())
							{
								//超过了需要的根数,则不需要去获取缓存
								if (it->second >= iTotalCount)
									bNeedMoreCache = false;
								else
								{
									//根数不够,需要去缓存服务器取数据. 取的根数就是总数与当前根数的差值, 截止时间就是当天第一根K线的时间戳-1
									iCacheCount = iTotalCount - it->second;
									iCacheEndTime = it5m->second.mapTimeCount.rbegin()->first - 1;
								}
							}
						}
						g_lock5MinKlineCount.unlock();
						//不需要更多缓存,则直接从本地缓存取,然后直接返回给客户端
						if (!bNeedMoreCache)
						{
							g_lock5MinKline.lock();
							auto it5m = g_map5MinKline.find(quoteId);
							if (it5m != g_map5MinKline.end())
							{
								it5m->second.GetCountKLines(iTotalCount, iEndTimeStamp, listResult);
							}
							g_lock5MinKline.unlock();
						}
						break;
					}
					//特别注意: 日线的开始和结束时间戳其实是交易日期,比如20190928
					case pbOPKLineType::DAY:
					{
						cacheKlineType = KLineType::KDAY;
						g_lockDayKline.lock();
						auto itDay = g_mapDayKLine.find(quoteId);
						if (itDay != g_mapDayKLine.end() && itDay->second.IsValid())
						{
							//日线要用交易日来判断
							int iTradeDate = atoi(itDay->second.strTradeDate.c_str());
							//请求的截止时间在当天日线的时间后面
							if (iTradeDate <= iEndTimeStamp)
							{
								//只请求一根
								if (iTotalCount == 1)
								{
									bNeedMoreCache = false;
									listResult.push_front(itDay->second);
								}
								else
								{
									iCacheCount = iTotalCount - 1;
									iCacheEndTime = iTradeDate - 1;
								}
							}
						}
						g_lockDayKline.unlock();
						break;
					}
					default:
						return;
						break;
					}
				}
				//auto t2 = std::chrono::steady_clock::now();
				//g_Logger->info("CCACHE reqid:{} 判断是否需要取缓存时间:{}微秒", cklineCountReq.irequestid(), std::chrono::duration<double, std::micro>(t2 - t1).count());
				//当天的数据足够,直接返回结果
				if (!bNeedMoreCache)
				{
					OPKLineCacheResponse cacheRsp;
					cacheRsp.set_irequestid(cklineCountReq.irequestid());
					for (const KLine& kline : listResult)
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
					string strOutput;
					cacheRsp.SerializeToString(&strOutput);

					string strResData;
					memcpy(detailType, "PBUF", sizeof(detailType));
					strResData.append(packet->protocl, sizeof(packet->protocl));
					strResData.append(detailType, sizeof(detailType));
					strResData.append(strOutput);
					strResData.append(s_strProtoDelimiter);
					//加包头
					ADD_PACKET_HEAD
				}
				else
				{
					//数据不够,需要去缓存服务器上面查询
					UINT32 iReqId = 0;
					if (g_pCacheClient->SendKLineCountReq(quoteId, iCacheEndTime, iCacheCount, cacheKlineType, iReqId))
					{
						g_lockClientKLineCache.lock();
						//如果iCacheEndTime != iEndTimeStamp, 说明需要补充本地数据
						//g_mapClientKLineCache[iReqId] = ClientKLineCache(quoteId, cklineCountReq.irequestid(), dwConnId, cacheKlineType, ProtocolType::CCACHE, iEndTimeStamp, iCacheEndTime != iEndTimeStamp);
						g_mapClientKLineCache[iReqId] = ClientKLineCache(quoteId, frontPacket->reqid, cklineCountReq.irequestid(), dwConnId, cacheKlineType, ProtocolType::CCACHE, iEndTimeStamp, iCacheEndTime != iEndTimeStamp, t1);
						g_lockClientKLineCache.unlock();
					}
				}
			}
		}
		//按照起止时间请求K线缓存
		else if (strProtocol.CompareNoCase("OPTCACHE") == 0)
		{
			auto t1 = std::chrono::steady_clock::now();
			OPKLineTimeRequest cklineTimeReq;
			if (cklineTimeReq.ParseFromArray(packet->data, iDataLength))
			{
				auto pbOptionid = cklineTimeReq.optionid();
				OptionID quoteId(pbOptionid.strexchange().c_str(), pbOptionid.strcomodityxyz().c_str(), pbOptionid.strcontractno().c_str(), pbOptionid.strcallorputflag()[0], pbOptionid.strstrikeprice().c_str());
				UINT32 iStartTimeStamp = cklineTimeReq.istarttimestamp();
				UINT32 iEndTimeStamp = cklineTimeReq.iendtimestamp();
				pbOPKLineType klineType = cklineTimeReq.type();
				KLineType cacheKlineType;
				bool bNeedMoreCache = true;
				list<KLine> listResult;
				UINT32 iCacheEndTime = iEndTimeStamp;
				if (iStartTimeStamp <= iEndTimeStamp)
				{
					switch (klineType)
					{
					case pbOPKLineType::M1:
					{
						cacheKlineType = KLineType::KM1;
						g_lock1MinKlineCount.lock();
						auto it1m = g_map1MinKlineCount.find(quoteId);
						if (it1m != g_map1MinKlineCount.end())
						{
							auto it = it1m->second.mapTimeCount.lower_bound(iEndTimeStamp);
							if (it != it1m->second.mapTimeCount.end())
							{
								//当天的时间区间能够覆盖所获取的时间区间
								if (it1m->second.mapTimeCount.rbegin()->first <= iStartTimeStamp)
									bNeedMoreCache = false;
								else
								{
									//当天的时间区间不能覆盖所获取的时间区间,需要去缓存服务器取数据. 取的 截止时间就是当天第一根K线的时间戳-1
									iCacheEndTime = it1m->second.mapTimeCount.rbegin()->first - 1;
								}
							}
						}
						g_lock1MinKlineCount.unlock();
						//不需要更多缓存,则直接从本地缓存取,然后直接返回给客户端
						if (!bNeedMoreCache)
						{
							g_lock1MinKline.lock();
							auto it1m = g_map1MinKlineDay.find(quoteId);
							if (it1m != g_map1MinKlineDay.end())
							{
								it1m->second.GetTimeKLines(iStartTimeStamp, iEndTimeStamp, listResult);
							}
							g_lock1MinKline.unlock();
						}
						break;
					}
					case pbOPKLineType::M5:
					{
						cacheKlineType = KLineType::KM5;
						g_lock5MinKlineCount.lock();
						auto it5m = g_map5MinKlineCount.find(quoteId);
						if (it5m != g_map5MinKlineCount.end())
						{
							auto it = it5m->second.mapTimeCount.lower_bound(iEndTimeStamp);
							if (it != it5m->second.mapTimeCount.end())
							{
								//当天的时间区间能够覆盖所获取的时间区间
								if (it5m->second.mapTimeCount.rbegin()->first <= iStartTimeStamp)
									bNeedMoreCache = false;
								else
								{
									//当天的时间区间不能覆盖所获取的时间区间,需要去缓存服务器取数据. 取的 截止时间就是当天第一根K线的时间戳-1
									iCacheEndTime = it5m->second.mapTimeCount.rbegin()->first - 1;
								}
							}
						}
						g_lock5MinKlineCount.unlock();
						//不需要更多缓存,则直接从本地缓存取,然后直接返回给客户端
						if (!bNeedMoreCache)
						{
							g_lock5MinKline.lock();
							auto it5m = g_map5MinKline.find(quoteId);
							if (it5m != g_map5MinKline.end())
							{
								it5m->second.GetTimeKLines(iStartTimeStamp, iEndTimeStamp, listResult);
							}
							g_lock5MinKline.unlock();
						}
						break;
					}
					//特别注意: 日线的开始和结束时间戳其实是交易日期,比如20190928
					case pbOPKLineType::DAY:
					{
						cacheKlineType = KLineType::KDAY;
						g_lockDayKline.lock();
						auto itDay = g_mapDayKLine.find(quoteId);
						if (itDay != g_mapDayKLine.end() && itDay->second.IsValid())
						{
							//日线要用交易日来判断
							int iTradeDate = atoi(itDay->second.strTradeDate.c_str());
							if (iTradeDate <= iStartTimeStamp)
							{
								bNeedMoreCache = false;
								listResult.push_front(itDay->second);
							}
							//日线的开始时间在起始时间后面,则需要更多的缓存数据
							else
							{
								if (iTradeDate <= iEndTimeStamp)
									iCacheEndTime = iTradeDate - 1;
							}
						}
						g_lockDayKline.unlock();
						break;
					}
					default:
						return;
						break;
					}
				}
				else
				{
					bNeedMoreCache = false;
				}
				//auto t2 = std::chrono::steady_clock::now();
				//g_Logger->info("CCACHE reqid:{} 判断是否需要取缓存时间:{}微秒", cklineTimeReq.irequestid(), std::chrono::duration<double, std::micro>(t2 - t1).count());
				if (!bNeedMoreCache)
				{
					OPKLineCacheResponse cacheRsp;
					cacheRsp.set_irequestid(cklineTimeReq.irequestid());
					for (const KLine& kline : listResult)
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
					string strOutput;
					cacheRsp.SerializeToString(&strOutput);

					string strResData;
					memcpy(detailType, "PBUF", sizeof(detailType));
					strResData.append(packet->protocl, sizeof(packet->protocl));
					strResData.append(detailType, sizeof(detailType));
					strResData.append(strOutput);
					strResData.append(s_strProtoDelimiter);
					//加包头
					ADD_PACKET_HEAD
				}
				else
				{
					//数据不够,需要去缓存服务器上面查询
					UINT32 iReqId = 0;
					if (g_pCacheClient->SendKLineTimeReq(quoteId, iStartTimeStamp, iCacheEndTime, cacheKlineType, iReqId))
					{
						g_lockClientKLineCache.lock();
						//如果iCacheEndTime != iEndTimeStamp, 说明需要补充本地数据
						//g_mapClientKLineCache[iReqId] = ClientKLineCache(quoteId, cklineTimeReq.irequestid(), dwConnId, cacheKlineType, ProtocolType::TCACHE, iEndTimeStamp, iCacheEndTime != iEndTimeStamp);
						g_mapClientKLineCache[iReqId] = ClientKLineCache(quoteId, frontPacket->reqid, cklineTimeReq.irequestid(), dwConnId, cacheKlineType, ProtocolType::TCACHE, iEndTimeStamp, iCacheEndTime != iEndTimeStamp, t1);
						g_lockClientKLineCache.unlock();
					}
				}
			}
		}
		//心跳
		else if (strProtocol.CompareNoCase("HBET") == 0)
		{
			resultBuffer.append(packet->protocl, sizeof(packet->protocl));
			resultBuffer.append(detailType, sizeof(detailType));
			resultBuffer.append(s_strProtoDelimiter);
		}		
	}
}
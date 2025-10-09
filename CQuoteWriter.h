/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   CQuoteWriter.h
 * Author: Administrator
 *
 * Created on 2018年5月9日, 下午2:35
 */

#ifndef CQUOTEWRITER_H
#define CQUOTEWRITER_H
#include <string>

using namespace std;

//文件类型
enum FileType
{
	//原始行情
	TXT,
	//处理过的行情
	TICK,
	//1分钟
	M1,
	//5分钟
	M5,
	//日线
	DAY
};
class CQuoteWriter {
public:
    CQuoteWriter();
    virtual ~CQuoteWriter();
	static void WriteQuote(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& quote, const string& tradeDate, bool bOriginal);
	//1分钟线是全量覆盖保存的。本地是一天一个文件,阿里云是一月一个文件。
	//本地的存储规则: 交易所/品种/交易日的年月/交易所_品种_合约_交易日.1m 比如:dig_test2/ZCE/AP/201905/ZCE_AP_05_20190507.1m
	//阿里云存储规则: 交易所/品种/交易日的年月/交易所_品种_合约.tick      比如:dig_test2/ZCE/AP/201904/ZCE_AP_05.1m
	static void Write1M(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate);
	//5分钟线是全量覆盖保存的。本地是一天一个文件,阿里云是一年一个文件。
	//本地的存储规则: 交易所/品种/交易日的年/交易所_品种_合约_交易日.5m 比如:dig_test2/ZCE/AP/2019/ZCE_AP_05_20190507.5m
	//阿里云存储规则: 交易所/品种/交易日的年/交易所_品种_合约.5m        比如:dig_test2/ZCE/AP/2019/ZCE_AP_05.5m
	static void Write5M(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate);
	//日线是也是覆盖保存的。本地是一天一个文件,阿里云是一个合约一个文件。
	//本地的存储规则: 交易所/品种/交易所_品种_合约_交易日.day  比如:dig_test2/ZCE/AP/ZCE_AP_05_20190507.day
	//阿里云存储规则: 交易所/品种/交易所_品种_合约.day		   比如:dig_test2/ZCE/AP/ZCE_AP_05.day
	static void WriteDay(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate);
};

#endif /* CQUOTEWRITER_H */


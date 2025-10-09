/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   CQuoteWriter.cpp
 * Author: Administrator
 * 
 * Created on 2018年5月9日, 下午2:35
 */

#include "CQuoteWriter.h"
#include "string.h"
#include "unistd.h"
#include "stdlib.h"
#include <stdio.h>
#include <sys/stat.h>
#include <map>

//extern map<CString, CString> g_mapProfile;
CQuoteWriter::CQuoteWriter() {
}
CQuoteWriter::~CQuoteWriter() 
{ 
}

//获取文件的本地路径 strDate的格式是yyyyMMdd
extern void GetLocalFilePath(const string& strExchage, const string& strComodity, const string& strContractNo, char cCallOrPutFlag, const string& strStrikePrice, FileType fileType, const string& strDate, string& path, string& fileName);
//获取文件的阿里云OSS路径
extern void GetAliOSSFilePath(const string& strExchage, const string& strComodity, const string& strContractNo, FileType fileType, const string& strDate, string& path, string& fileName);

int createMultiLevelDir(const char* sPathName)  
{  
    char DirName[256];      
    int i, len;

    strcpy(DirName, sPathName);      
    len = strlen(DirName);             
    if('/' != DirName[len-1]) {  
        strcat(DirName, "/"); 
        len++;
    }           

    for(i=1; i<len; i++)      
    {      
        if('/' == DirName[i])      
        {      
            DirName[i] = '\0';      
            if(access(DirName, F_OK) != 0)      
            {      
                if(mkdir(DirName, 0777) == -1)      
                {       
                   return -1;       
                }      
            }    
            DirName[i] = '/';      
         }      
  }    

  return 0;      
}  

void CQuoteWriter::WriteQuote(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& quote, const string& tradeDate, bool bOriginal)
{
	if (quote.empty())
		return;
	string path, fileName;
	if (bOriginal)
	{
		GetLocalFilePath(exchange, cmdt, contractNo, cCallOrPutFlag, strStrikePrice, FileType::TXT, tradeDate, path, fileName);
		//path = "/usr/ejia7/original_test2/";
		//path += exchange + "/";
		//path += cmdt + "/";
		//path += contractNo + "/";

		//fileName = path + tradeDate;
		//fileName += ".txt";
	}
	else
	{
		GetLocalFilePath(exchange, cmdt, contractNo, cCallOrPutFlag, strStrikePrice, FileType::TICK, tradeDate, path, fileName);
		//path = "/usr/ejia7/dig_test2/";
		//path += exchange + "/";
		//path += cmdt + "/";
		//path += contractNo + "/";

		//fileName = path + tradeDate;
		//fileName += ".tick";
	}
    FILE* pFile = NULL;
    if(createMultiLevelDir(path.c_str())<0)
    {
         return;
    }    
    pFile = fopen(fileName.c_str(),"a+");  
	if (pFile)
	{
		fputs(quote.c_str(), pFile);
		fclose(pFile);
	}
	else
	{
		//g_Logger->error("{}->文件打开失败: {}", __FUNCTION__, fileName.c_str());
	}
}
void CQuoteWriter::Write1M(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate)
{
	if (content.empty())
		return;
	string path, fileName;
	GetLocalFilePath(exchange, cmdt, contractNo, cCallOrPutFlag, strStrikePrice, FileType::M1, tradeDate, path, fileName);
	//path = "/usr/ejia7/dig_test2/";
	////交易所
	//path += exchange + "/";
	////品种
	//path += cmdt + "/";
	////交易日的年月
	//path += tradeDate.substr(0, 6) + "/";

	//fileName = path + exchange + "_" + cmdt + "_" + contractNo + "_" + tradeDate + ".1m";

	FILE* pFile = NULL;
	if (createMultiLevelDir(path.c_str()) < 0)
	{
		return;
	}
	//覆盖写入文件
	pFile = fopen(fileName.c_str(), "w");
	if (pFile)
	{
		fputs(content.c_str(), pFile);
		fclose(pFile);
	}
	else
	{
		//g_Logger->error("{}->文件打开失败: {}", __FUNCTION__, fileName.c_str());
	}
}
void CQuoteWriter::Write5M(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate)
{
	if (content.empty())
		return;
	string path, fileName;
	GetLocalFilePath(exchange, cmdt, contractNo, cCallOrPutFlag, strStrikePrice, FileType::M5, tradeDate, path, fileName);
	//path = "/usr/ejia7/dig_test2/";
	////交易所
	//path += exchange + "/";
	////品种
	//path += cmdt + "/";
	////交易日的年
	//path += tradeDate.substr(0, 4) + "/";

	//fileName = path + exchange + "_" + cmdt + "_" + contractNo + "_" + tradeDate + ".5m";

	FILE* pFile = NULL;
	if (createMultiLevelDir(path.c_str()) < 0)
	{
		return;
	}
	//覆盖写入文件
	pFile = fopen(fileName.c_str(), "w");
	if (pFile)
	{
		fputs(content.c_str(), pFile);
		fclose(pFile);
	}
	else
	{
		//g_Logger->error("{}->文件打开失败: {}", __FUNCTION__, fileName.c_str());
	}
}
void CQuoteWriter::WriteDay(const string& exchange, const string& cmdt, const string& contractNo, char cCallOrPutFlag, const string& strStrikePrice, const string& content, const string& tradeDate)
{
	if (content.empty())
		return;
	string path, fileName;
	GetLocalFilePath(exchange, cmdt, contractNo, cCallOrPutFlag, strStrikePrice, FileType::DAY, tradeDate, path, fileName);
	//path = "/usr/ejia7/dig_test2/";
	////交易所
	//path += exchange + "/";
	////品种
	//path += cmdt + "/";

	//fileName = path + exchange + "_" + cmdt + "_" + contractNo + "_" + tradeDate + ".day";

	FILE* pFile = NULL;
	if (createMultiLevelDir(path.c_str()) < 0)
	{
		return;
	}
	//覆盖写入文件
	pFile = fopen(fileName.c_str(), "w");
	if (pFile)
	{
		fputs(content.c_str(), pFile);
		fclose(pFile);
	}
	else
	{
		//g_Logger->error("{}->文件打开失败: {}", __FUNCTION__, fileName.c_str());
	}
}
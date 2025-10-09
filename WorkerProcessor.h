#pragma once
#include "DataStruct.h"

//包命令
typedef enum {
	cmd_WPHQ = 0,					//外盘实时行情,需要网关服务自己处理
	cmd_NO_NEED_HANDLE = 1,			//需要网关服务根据reqid直接转发数据
	cmd_NPHQ = 2,					//内盘实时行情,需要网关服务自己处理
	cmd_OPHQ = 3					//期权实时行情,需要网关服务自己处理
} cc_cmd_t;

//通用包结构
#pragma pack(1)
typedef struct {
	unsigned char	cmd;	//	网关用来区分该数据包是需要转发给客户端还是自己处理
	UINT32			reqid;	//	请求id
	UINT32			size;	//	包体长度
	unsigned char	data[0];//	客户端的原始请求数据. protocl+detailType+data
}
front_packet_t;

typedef struct {
	char	protocl[8];		//协议名字
	char	detailType[4];	//协议格式  TXT、GZIP等
	char	data[0];		//协议内容
}
data_packet_t;
//实时行情的数据结构。 发送的时候就填上交易所、品种和合约代码,前置网关收到以后就不用再解析行情的全文来获取这些信息
typedef struct {
	char	exchange[8];	//交易所
	char	comodity[4];	//品种
	char	contractNo[8];	//合约
	char	callOrPutFlag;	//买权 卖权
	char	strikePrice[12];//行权价格
	char	data[0];		//行情全文
}
ophq_packet_t;
#pragma pack()
#define ADD_PACKET_HEAD		front_packet_t res_pkt;\
							res_pkt.cmd = cmd_NO_NEED_HANDLE;\
							res_pkt.reqid = frontPacket->reqid;\
							res_pkt.size = htonl(strResData.size());\
							resultBuffer.append(string((LPCSTR)&res_pkt, sizeof(res_pkt)));\
							resultBuffer.append(strResData);

class WorkerProcessor
{
public:
	WorkerProcessor();
	~WorkerProcessor();

public:
	//判断数据包是否完整，如果完整返回完整数据包的长度，否则返回-1
	static int get_length(const BYTE* bData, int nDataLen);
	//业务处理。根据包头命令，执行相应操作
	static void Parse(const BYTE* bData, int iDataLen, ULONG dwConnId, string& resultBuffer, bool& bForceDisconnect);
	//登录时正确的密码
	static string s_strRightPwd;
};


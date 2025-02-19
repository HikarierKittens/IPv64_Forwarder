#include <string>
#include <winsock2.h>
#include "conlog.h"
#include <iostream>
#include <thread> // 包含线程库
#include <chrono> // 包含chrono库，用于处理时间




void LogSocketError(int errorCode) {
    switch (errorCode) {
    case WSAECONNABORTED:
        Log("连接已被一方终止 (WSAECONNABORTED, 10053)");
        break;
    case WSAECONNRESET:
        Log("连接被对方重置 (WSAECONNRESET, 10054)");
        break;
    case WSAEHOSTUNREACH:
        Log("目标主机无法访问 (WSAEHOSTUNREACH, 10065)");
        break;
    case WSAENETDOWN:
        Log("网络子系统不可用 (WSAENETDOWN, 10050)");
        break;
    case WSAENETRESET:
        Log("网络连接已被重置 (WSAENETRESET, 10052)");
        break;
    case WSAENETUNREACH:
        Log("网络不可达 (WSAENETUNREACH, 10051)");
        break;
    case WSAETIMEDOUT:
        Log("连接超时 (WSAETIMEDOUT, 10060)");
        break;
    case WSATYPE_NOT_FOUND:
        Log("未知的地址类型 (WSATYPE_NOT_FOUND, 10109)");
        Log("请检查配置文件是否正确");
        break;
        // 可以在这里添加更多的错误处理
    default:
        Log(("出现未知错误，错误代码: " + std::to_string(errorCode)).c_str());
        break;
    }
}

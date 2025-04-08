#include <winsock2.h>    // Winsock API的头文件，用于网络编程
#include <ws2tcpip.h>    // 提供了一些新的函数和数据结构，增强网络编程能力
#include <iostream>       // 标准输入输出流库
#include <thread>         // C++11中的线程库
#include <vector>         // 动态数组库
#include <fstream>        // 文件流库，用于文件操作
#include <nlohmann/json.hpp>  // nlohmann的json库，用于json数据的解析和生成
#include <filesystem>      // C++17中的文件系统库，用于文件系统的操作
#include "conlog.h"        // 自定义的日志记录库

using json = nlohmann::json;  // 使用json作为nlohmann::json的别名
namespace fs = std::filesystem;  // 使用fs作为std::filesystem的别名
//using namespace std;  //不要碰这个，一旦加上这个，就会报错

// 定义转发规则结构体，包含名称、监听地址、目标地址和协议类型
struct ForwardRule {
    std::string name;
    std::string listen;
    std::string target;
    std::string protocol;
};

// 日志队列，用于存储日志消息，声明为全局变量以便所有函数访问
SemaphoreQueue<std::string> logQueue;
bool logWorkerRunning = true;  // 日志工作线程的运行状态标志

// 日志工作线程函数，负责从队列中取出日志消息并输出到控制台
void LogWorker() {
    while (logWorkerRunning) {  // 只要日志工作线程正在运行，就不断循环获取日志消息
        std::string logMessage = logQueue.Dequeue();  // 从日志队列中取出一条消息
        std::cout << logMessage << std::endl;  // 将消息输出到控制台
    }
}

// 日志记录函数，将日志消息加入队列
void Log(const std::string& message) {
    logQueue.Enqueue(message);  // 将日志消息加入队列中
}

// 创建套接字的函数，根据给定的地址信息创建相应的套接字
SOCKET CreateSocket(const addrinfo* info) {
    SOCKET sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);  // 创建套接字
    if (sock == INVALID_SOCKET) {  // 如果创建失败
        LogSocketError(WSAGetLastError());  // 记录错误信息
        return INVALID_SOCKET;  // 返回无效套接字
    }

    int optval = 1;
    // 设置套接字选项，允许地址重用
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录错误信息
        closesocket(sock);  // 关闭套接字
        return INVALID_SOCKET;  // 返回无效套接字
    }

    // 绑定套接字到指定的地址信息
    if (bind(sock, info->ai_addr, (int)info->ai_addrlen) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录错误信息
        closesocket(sock);  // 关闭套接字
        return INVALID_SOCKET;  // 返回无效套接字
    }

    return sock;  // 返回创建并绑定成功的套接字
}

// 转发TCP连接的函数，将客户端连接的数据转发到目标服务器，并将服务器的数据转发回客户端
void ForwardTCP(SOCKET client, const sockaddr_storage& targetAddr) {
    SOCKET server = socket(targetAddr.ss_family, SOCK_STREAM, IPPROTO_TCP);  // 创建一个TCP套接字用于连接目标服务器
    if (server == INVALID_SOCKET) {  // 如果创建失败
        LogSocketError(WSAGetLastError());  // 记录错误信息
        closesocket(client);  // 关闭客户端套接字
        return;  // 返回
    }

    // 连接到目标服务器
    if (connect(server, (sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录错误信息
        closesocket(client);  // 关闭客户端套接字
        closesocket(server);  // 关闭服务器套接字
        return;  // 返回
    }

    // 定义一个lambda函数用于数据转发
    auto forward = [](SOCKET from, SOCKET to) {
        char buffer[4096];  // 定义缓冲区用于存储接收到的数据
        while (true) {  // 不断循环，直到转发结束
            int len = recv(from, buffer, sizeof(buffer), 0);  // 接收数据
            if (len <= 0) {  // 如果接收失败或连接关闭
                if (len == 0) {
                    Log("Connection closed by peer.");  // 记录连接关闭信息
                }
                else {
                    LogSocketError(WSAGetLastError());  // 记录错误信息
                }
                break;  // 结束循环
            }
            // 将接收到的数据转发到另一个套接字
            if (send(to, buffer, len, 0) == SOCKET_ERROR) {
                LogSocketError(WSAGetLastError());  // 记录错误信息
                break;  // 结束循环
            }
        }
        closesocket(from);  // 关闭源套接字
        closesocket(to);  // 关闭目标套接字
        };

    // 启动两个线程，分别从客户端到服务器，服务器到客户端转发数据
    std::thread(forward, client, server).detach();
    std::thread(forward, server, client).detach();
}

// 处理UDP数据包的函数，将从一个地址接收的UDP数据包发送到另一个地址，并将响应发送回原始地址
void HandleUDP(SOCKET sock, const sockaddr_storage& targetAddr) {
    char buffer[4096];  // 定义缓冲区用于存储接收到的数据
    sockaddr_storage clientAddr;  // 用于存储客户端地址信息
    int addrLen = sizeof(clientAddr);  // 客户端地址信息的长度

    while (true) {  // 不断循环，直到处理结束
        // 接收UDP数据包
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &addrLen);
        if (len <= 0) {  // 如果接收失败或连接关闭
            if (len == 0) {
                Log("Client disconnected.");  // 记录客户端断开信息
            }
            else {
                LogSocketError(WSAGetLastError());  // 记录错误信息
            }
            break;  // 结束循环
        }

        char clientIP[NI_MAXHOST];
        // 获取客户端的IP地址
        getnameinfo((sockaddr*)&clientAddr, addrLen, clientIP, sizeof(clientIP), nullptr, 0, NI_NUMERICHOST);
        Log("接收到来自 " + std::string(clientIP) + " 的 UDP 连接");  // 记录接收到的UDP连接信息

        // 将接收到的数据发送到目标地址
        if (sendto(sock, buffer, len, 0, (sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
            LogSocketError(WSAGetLastError());  // 记录错误信息
            break;  // 结束循环
        }

        // 接收目标地址的响应，并发送回客户端
        len = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (len > 0) {
            if (sendto(sock, buffer, len, 0, (sockaddr*)&clientAddr, addrLen) == SOCKET_ERROR) {
                LogSocketError(WSAGetLastError());  // 记录错误信息
                break;  // 结束循环
            }
        }
    }
    closesocket(sock);  // 关闭套接字
}

// 解析地址和端口的函数，根据地址和端口字符串生成addrinfo结构体
addrinfo* ResolveAddress(const std::string& address, const std::string& port, int sockType, bool passive = false) {
    addrinfo hints{}, * result = nullptr;  // hints用于指定getaddrinfo函数的行为，result用于存储结果
    ZeroMemory(&hints, sizeof(hints));  // 将hints清零
    hints.ai_flags = passive ? AI_PASSIVE : 0;  // 设置地址信息标志，用于被动打开（服务器模式）
    hints.ai_family = AF_UNSPEC;  // 允许IPv4或IPv6
    hints.ai_socktype = sockType;  // 设置套接字类型，TCP或UDP

    // 使用getaddrinfo函数解析地址和端口
    if (getaddrinfo(address.c_str(), port.c_str(), &hints, &result) != 0) {
        LogSocketError(WSAGetLastError());  // 记录错误信息
        return nullptr;  // 返回空指针
    }
    return result;  // 返回解析结果
}

// 开始根据规则进行转发的函数，根据ForwardRule中的信息启动相应的转发逻辑
void StartForwarding(const ForwardRule& rule) {
    std::string listen_Address, listen_Port;
    // 分离监听地址和端口
    SeparateIpAndPort_listen(rule.listen, listen_Address, listen_Port);

    // 解析监听地址和端口，并获取对应的addrinfo结构体
    addrinfo* listenInfo = ResolveAddress(listen_Address, listen_Port,
        (rule.protocol == "tcp") ? SOCK_STREAM : SOCK_DGRAM, true);  // 根据协议类型选择套接字类型
    if (!listenInfo) return;  // 如果解析失败，返回

    std::string target_Address, target_Port;
    // 分离目标地址和端口
    SeparateIpAndPort_target(rule.target, target_Address, target_Port);

    // 解析目标地址和端口，并获取对应的addrinfo结构体
    addrinfo* targetInfo = ResolveAddress(target_Address, target_Port,
        (rule.protocol == "tcp") ? SOCK_STREAM : SOCK_DGRAM);  // 根据协议类型选择套接字类型
    if (!targetInfo) {
        freeaddrinfo(listenInfo);  // 释放监听地址信息
        return;  // 如果解析失败，返回
    }

    // 创建监听套接字
    SOCKET listenSocket = CreateSocket(listenInfo);
    if (listenSocket == INVALID_SOCKET) {
        freeaddrinfo(listenInfo);  // 释放监听地址信息
        freeaddrinfo(targetInfo);  // 释放目标地址信息
        return;  // 如果创建失败，返回
    }

    // 省略后续逻辑...
    // 根据解析的地址信息，开始监听并处理连接
    // 这里省略了监听TCP连接并创建新的线程来处理每个连接的代码

    freeaddrinfo(listenInfo);  // 释放监听地址信息
    freeaddrinfo(targetInfo);  // 释放目标地址信息
}

// 未发现配置文件时创建默认配置文件的函数
void CreateDefaultConfig(const std::string& filePath) {
    Log("未发现配置文件，已创建默认配置文件至当前工作目录:" + filePath);  // 记录日志信息，提示创建默认配置文件
    json defaultConfig = {  // 创建默认配置内容
        {"forward_rules", {  // 转发规则数组
            {
                {"name", "example1_rule"},  // 规则名称
                {"listen", "127.0.0.1:19555"},  // 监听地址和端口
                {"target", "127.0.0.1:19666"},  // 目标地址和端口
                {"protocol", "tcp"}  // 协议类型
            },
            {
                {"name", "example2_rule"},
                {"listen", "[::1]:19777"},
                {"target", "[::1]:19888"},
                {"protocol", "tcp"}
            },
        }}
    };

    std::ofstream configFile(filePath);  // 创建输出文件流用于写入配置文件
    if (!configFile.is_open()) {
        Log("配置文件创建失败");  // 记录日志信息，提示配置文件创建失败
        return;  // 如果文件流打开失败，返回
    }
    configFile << defaultConfig.dump(4);  // 将默认配置内容以格式化的json字符串写入文件
    configFile.close();  // 关闭文件流
}

// 获取可执行文件的路径的函数
std::string GetExecutablePath() {
    char buffer[MAX_PATH];  // 定义缓冲区用于存储可执行文件路径
    // 使用GetModuleFileNameA函数获取可执行文件的路径
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    // 找到最后一个路径分隔符的位置，以截取路径部分
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);  // 返回路径部分
}

// 主函数，程序入口
int main() {
    system("chcp 65001");  // 设置控制台字符编码为UTF-8

    std::thread logThread(LogWorker);  // 创建日志工作线程
    logThread.detach();  // 分离线程，使其在后台独立运行

    std::string exePath = GetExecutablePath();  // 获取可执行文件的路径
    fs::current_path(exePath);  // 设置当前工作目录为可执行文件的路径

    WSADATA wsa;
    // 初始化Winsock库
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("WSAStartup failed.");  // 记录日志信息，提示Winsock初始化失败
        return 1;  // 返回1表示程序失败
    }

    std::string configFilePath = "config.json";  // 定义配置文件路径
    if (!fs::exists(configFilePath)) {  // 如果配置文件不存在
        Log("Config file not found. Creating default config file.");  // 记录日志信息，提示未发现配置文件并准备创建默认配置文件
        CreateDefaultConfig(configFilePath);  // 创建默认配置文件
    }

    std::ifstream configFile(configFilePath);  // 创建输入文件流用于读取配置文件
    if (!configFile.is_open()) {
        Log("Failed to open config file.");  // 记录日志信息，提示配置文件打开失败
        WSACleanup();  // 清理Winsock资源
        return 1;  // 返回1表示程序失败
    }

    json config;
    try {
        config = json::parse(configFile);  // 解析配置文件内容
    }
    catch (const json::parse_error& e) {
        Log("Failed to parse config file: " + std::string(e.what()));  // 记录日志信息，提示配置文件解析失败
        WSACleanup();  // 清理Winsock资源
        return 1;  // 返回1表示程序失败
    }

    std::vector<ForwardRule> rules;
    // 从配置文件中读取转发规则，并存储到rules数组中
    if (config.contains("forward_rules") && config["forward_rules"].is_array()) {
        for (const auto& rule : config["forward_rules"]) {
            if (rule.contains("name") && rule.contains("listen") && rule.contains("target") && rule.contains("protocol")) {
                rules.push_back({  // 将规则添加到数组中
                    rule["name"].get<std::string>(),  // 获取规则名称
                    rule["listen"].get<std::string>(),  // 获取监听地址和端口
                    rule["target"].get<std::string>(),  // 获取目标地址和端口
                    rule["protocol"].get<std::string>()  // 获取协议类型
                    });
            }
            else {
                Log("Invalid rule format in config file--配置文件读取错误.");  // 记录日志信息，提示规则格式无效
            }
        }
    }
    else {
        Log("No valid forward_rules found in config file.");  // 记录日志信息，提示未找到有效的转发规则
logWorkerRunning = false;
if (logThread.joinable()) {
    logThread.join();
}
    }

    // 根据规则数组中的每个规则启动转发逻辑
    for (const auto& rule : rules) {
        StartForwarding(rule);
    }

    Log("Port forwarder running. Press Enter to exit...");  // 记录日志信息，提示端口转发器正在运行，并等待用户输入
    std::cin.get();  // 等待用户按下回车键

    logWorkerRunning = false;  // 设置日志工作线程的运行状态为停止
    WSACleanup();  // 清理Winsock资源
    return 0;  // 返回0表示程序成功
}

#include <winsock2.h>  // 包含Windows套接字库的头文件
#include <ws2tcpip.h>  // 包含Windows套接字库的扩展功能头文件
#include <iostream>     // 包含标准输入输出流库
#include <thread>       // 包含多线程支持库
#include <vector>       // 包含向量容器库
#include <fstream>      // 包含文件流库
#include <nlohmann/json.hpp>  // 包含nlohmann的JSON库
#include <filesystem>         // 包含C++17文件系统库
#include "conlog.h"           // 包含自定义的日志处理库

using json = nlohmann::json;  // 使用nlohmann的json命名空间
namespace fs = std::filesystem;  // 使用C++17的filesystem命名空间
using std::cout;  // 使用标准输出流
using std::cin;   // 使用标准输入流

// 定义转发规则结构体，包含规则名称、监听地址、目标地址和协议类型
struct ForwardRule {
    std::string name;  // 转发规则的名称
    std::string listen;  // 监听地址和端口，格式为 "IP:Port"
    std::string target;  // 目标地址和端口，格式为 "IP:Port"
    std::string protocol;  // 协议类型，目前支持 "tcp" 和 "udp"
};

// 定义日志队列和日志工作线程运行标志
SemaphoreQueue<std::string> logQueue;  // 用于存储日志消息的队列
bool logWorkerRunning = true;  // 日志工作线程的运行标志

// 日志工作线程，从队列中取出日志消息并打印
void LogWorker() {
    while (logWorkerRunning) {
        std::string logMessage = logQueue.Dequeue();  // 从队列中获取一条日志消息
        std::cout << logMessage << std::endl;  // 打印日志消息
    }
}

// 将日志消息加入队列
void Log(const std::string& message) {
    logQueue.Enqueue(message);  // 将日志消息加入队列
}

// 创建套接字并进行一些初始化设置，如地址重用和绑定
SOCKET CreateSocket(const addrinfo* info) {
    SOCKET sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);  // 创建套接字
    if (sock == INVALID_SOCKET) {
        LogSocketError(WSAGetLastError());  // 记录套接字创建失败的错误信息
        return INVALID_SOCKET;  // 返回无效套接字
    }

    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录设置套接字选项失败的错误信息
        closesocket(sock);  // 关闭套接字
        return INVALID_SOCKET;  // 返回无效套接字
    }

    if (bind(sock, info->ai_addr, (int)info->ai_addrlen) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录绑定套接字失败的错误信息
        closesocket(sock);  // 关闭套接字
        return INVALID_SOCKET;  // 返回无效套接字
    }

    return sock;  // 返回创建并成功初始化的套接字
}

// 转发TCP连接的数据，创建两个线程分别处理客户端到服务器和服务器到客户端的数据转发
void ForwardTCP(SOCKET client, const sockaddr_storage& targetAddr) {
    SOCKET server = socket(targetAddr.ss_family, SOCK_STREAM, IPPROTO_TCP);  // 创建目标服务器的套接字
    if (server == INVALID_SOCKET) {
        LogSocketError(WSAGetLastError());  // 记录套接字创建失败的错误信息
        closesocket(client);  // 关闭客户端套接字
        return;
    }

    if (connect(server, (sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
        LogSocketError(WSAGetLastError());  // 记录连接目标服务器失败的错误信息
        closesocket(client);  // 关闭客户端套接字
        closesocket(server);  // 关闭服务器套接字
        return;
    }

    // 定义一个lambda函数用于数据转发
    auto forward = [](SOCKET from, SOCKET to) {
        char buffer[4096];  // 缓冲区用于存储接收的数据
        while (true) {
            int len = recv(from, buffer, sizeof(buffer), 0);  // 从from套接字接收数据
            if (len <= 0) {
                if (len == 0) {
                    Log("Connection closed by peer.");  // 如果len为0，表示连接已关闭
                }
                else {
                    LogSocketError(WSAGetLastError());  // 记录接收数据失败的错误信息
                }
                break;  // 退出循环
            }
            if (send(to, buffer, len, 0) == SOCKET_ERROR) {
                LogSocketError(WSAGetLastError());  // 记录发送数据失败的错误信息
                break;  // 退出循环
            }
        }
        closesocket(from);  // 关闭from套接字
        closesocket(to);    // 关闭to套接字
        };

    std::thread(forward, client, server).detach();  // 启动一个线程从客户端转发数据到服务器
    std::thread(forward, server, client).detach();  // 启动一个线程从服务器转发数据到客户端
}

// 处理UDP数据包的接收和转发
void HandleUDP(SOCKET sock, const sockaddr_storage& targetAddr) {
    char buffer[4096];  // 缓冲区用于存储接收的数据
    sockaddr_storage clientAddr;  // 存储客户端地址信息
    int addrLen = sizeof(clientAddr);  // 客户端地址信息的长度

    while (true) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &addrLen);  // 接收UDP数据包
        if (len <= 0) {
            if (len == 0) {
                Log("Client disconnected.");  // 如果len为0，表示客户端已断开连接
            }
            else {
                LogSocketError(WSAGetLastError());  // 记录接收数据失败的错误信息
            }
            break;  // 退出循环
        }

        char clientIP[NI_MAXHOST];  // 存储客户端IP地址的数组
        getnameinfo((sockaddr*)&clientAddr, addrLen, clientIP, sizeof(clientIP), nullptr, 0, NI_NUMERICHOST);  // 获取客户端IP地址
        Log("接收到来自 " + std::string(clientIP) + " 的 UDP 连接");  // 记录接收到来自客户端的UDP数据包

        if (sendto(sock, buffer, len, 0, (sockaddr*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
            LogSocketError(WSAGetLastError());  // 记录发送数据失败的错误信息
            break;  // 退出循环
        }

        // 接收并转发客户端的响应数据包
        len = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (len > 0) {
            if (sendto(sock, buffer, len, 0, (sockaddr*)&clientAddr, addrLen) == SOCKET_ERROR) {
                LogSocketError(WSAGetLastError());  // 记录发送数据失败的错误信息
                break;  // 退出循环
            }
        }
    }
    closesocket(sock);  // 关闭套接字
}

// 根据配置规则启动转发服务
void StartForwarding(const ForwardRule& rule) {
    addrinfo hints{}, * listenInfo = nullptr, * targetInfo = nullptr;  // 定义addrinfo结构体变量

    // 分离监听地址和端口
    std::string listen_Address, listen_Port;
    SeparateIpAndPort_listen(rule.listen, listen_Address, listen_Port);

    ZeroMemory(&hints, sizeof(hints));  // 清空hints结构体
    hints.ai_flags = AI_PASSIVE;  // 设置AI_PASSIVE标志，用于绑定套接字
    hints.ai_family = AF_UNSPEC;  // 设置地址族为未指定，自动选择IPv4或IPv6
    hints.ai_socktype = (rule.protocol == "tcp") ? SOCK_STREAM : SOCK_DGRAM;  // 根据协议类型设置套接字类型

    // 解析监听地址
    Log("Resolving listen address: " + listen_Address + ":" + listen_Port);  // 记录正在解析监听地址的信息
    if (getaddrinfo(listen_Address.c_str(), listen_Port.c_str(), &hints, &listenInfo) != 0) {
        LogSocketError(WSAGetLastError());  // 记录解析地址失败的错误信息
        return;
    }

    // 分离目标地址和端口
    std::string target_Address, target_Port;
    SeparateIpAndPort_target(rule.target, target_Address, target_Port);

    ZeroMemory(&hints, sizeof(hints));  // 再次清空hints结构体
    hints.ai_family = AF_UNSPEC;  // 设置地址族为未指定，自动选择IPv4或IPv6
    hints.ai_socktype = (rule.protocol == "tcp") ? SOCK_STREAM : SOCK_DGRAM;  // 根据协议类型设置套接字类型

    // 解析目标地址
    Log("Resolving target address: " + target_Address + ":" + target_Port);  // 记录正在解析目标地址的信息
    if (getaddrinfo(target_Address.c_str(), target_Port.c_str(), &hints, &targetInfo) != 0) {
        LogSocketError(WSAGetLastError());  // 记录解析地址失败的错误信息
        freeaddrinfo(listenInfo);  // 释放监听地址信息
        return;
    }

    // 创建监听套接字
    SOCKET listenSocket = CreateSocket(listenInfo);
    if (listenSocket == INVALID_SOCKET) {
        freeaddrinfo(listenInfo);  // 释放监听地址信息
        freeaddrinfo(targetInfo);  // 释放目标地址信息
        return;  // 返回
    }

    // 如果协议是TCP
    if (rule.protocol == "tcp") {
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            Log("Failed to listen on socket: " + std::to_string(WSAGetLastError()));  // 记录监听失败的错误信息
            closesocket(listenSocket);  // 关闭监听套接字
            freeaddrinfo(listenInfo);  // 释放监听地址信息
            freeaddrinfo(targetInfo);  // 释放目标地址信息
            return;
        }

        // 启动一个线程接受客户端连接
        std::thread([=] {
            while (true) {
                sockaddr_storage clientAddr;  // 存储客户端地址信息
                int addrLen = sizeof(clientAddr);  // 客户端地址信息的长度
                SOCKET client = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);  // 接受客户端连接
                if (client == INVALID_SOCKET) {
                    Log("Failed to accept client connection: " + std::to_string(WSAGetLastError()));  // 记录接受连接失败的错误信息
                    continue;  // 继续循环
                }
                char clientIP[NI_MAXHOST];  // 存储客户端IP地址的数组
                getnameinfo((sockaddr*)&clientAddr, addrLen, clientIP, sizeof(clientIP), nullptr, 0, NI_NUMERICHOST);  // 获取客户端IP地址
                Log("接收到来自 " + std::string(clientIP) + " 的 TCP 连接");  // 记录接收到来自客户端的TCP连接

                // 启动一个线程进行TCP数据转发
                if (targetInfo != nullptr && targetInfo->ai_addr != nullptr) {
                    std::thread(ForwardTCP, client, *(sockaddr_storage*)targetInfo->ai_addr).detach();
                }
                else {
                    Log("targetInfo 或 targetInfo->ai_addr 是空指针");  // 记录目标地址信息为空指针的情况
                }
            }
            closesocket(listenSocket);  // 关闭监听套接字
            }).detach();  // 启动线程并分离
    }
    else {  // 如果协议是UDP
        // 启动一个线程处理UDP数据包的接收和转发
        std::thread(HandleUDP, listenSocket, *(sockaddr_storage*)targetInfo->ai_addr).detach();
    }

    freeaddrinfo(listenInfo);  // 释放监听地址信息
    freeaddrinfo(targetInfo);  // 释放目标地址信息
}

// 创建默认配置文件，包含两个示例转发规则
void CreateDefaultConfig(const std::string& filePath) {
    json defaultConfig = {
        {"forward_rules", {
            {
                {"name", "example1_rule"},  // 规则名称
                {"listen", "127.0.0.1:19555"},  // 监听地址和端口
                {"target", "127.0.0.1:19666"},  // 目标地址和端口
                {"protocol", "tcp"}  // 协议类型
            },
            {
                {"name", "example2_rule"},  // 规则名称
                {"listen", "[::1]:19777"},  // 监听地址和端口（IPv6）
                {"target", "[::1]:19888"},  // 目标地址和端口（IPv6）
                {"protocol", "tcp"}  // 协议类型
            },
        }}
    };

    std::ofstream configFile(filePath);  // 创建文件流用于写入配置文件
    if (!configFile.is_open()) {
        Log("Failed to create default config file.");  // 记录创建默认配置文件失败的错误信息
        return;
    }
    configFile << defaultConfig.dump(4);  // 将默认配置写入文件，格式化输出，缩进为4个空格
    configFile.close();  // 关闭文件流
}

// 获取可执行文件的路径
std::string GetExecutablePath() {
    char buffer[MAX_PATH];  // 定义一个数组用于存储路径
    GetModuleFileNameA(NULL, buffer, MAX_PATH);  // 获取可执行文件的路径
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");  // 找到最后一个分隔符的位置
    return std::string(buffer).substr(0, pos);  // 返回可执行文件所在的目录路径
}

// 主函数，初始化Winsock库，读取配置文件并启动相应的转发服务
int main() {
    SetConsoleOutputCP(CP_UTF8);  // 设置控制台输出为UTF-8编码，支持中文

    std::thread logThread(LogWorker);  // 启动日志工作线程
    logThread.detach();  // 分离线程，使其独立运行

    std::string exePath = GetExecutablePath();  // 获取可执行文件的路径
    fs::current_path(exePath);  // 设置当前工作目录为可执行文件所在的目录

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {  // 初始化Winsock库
        Log("WSAStartup failed.");  // 记录初始化失败的错误信息
        return 1;  // 返回错误码
    }

    std::string configFilePath = "config.json";  // 定义配置文件的路径
    if (!fs::exists(configFilePath)) {  // 检查配置文件是否存在
        Log("Config file not found. Creating default config file.");  // 记录配置文件不存在的情况
        CreateDefaultConfig(configFilePath);  // 创建默认配置文件
    }

    std::ifstream configFile(configFilePath);  // 创建文件流用于读取配置文件
    if (!configFile.is_open()) {
        Log("Failed to open config file.");  // 记录打开配置文件失败的错误信息
        WSACleanup();  // 清理Winsock库
        return 1;  // 返回错误码
    }

    json config;
    try {
        config = json::parse(configFile);  // 解析配置文件内容
    }
    catch (const json::parse_error& e) {
        Log("Failed to parse config file: " + std::string(e.what()));  // 记录解析配置文件失败的错误信息
        WSACleanup();  // 清理Winsock库
        return 1;  // 返回错误码
    }

    std::vector<ForwardRule> rules;  // 定义一个向量用于存储转发规则
    if (config.contains("forward_rules") && config["forward_rules"].is_array()) {  // 检查配置文件中是否存在转发规则数组
        for (const auto& rule : config["forward_rules"]) {  // 遍历转发规则数组
            if (rule.contains("name") && rule.contains("listen") && rule.contains("target") && rule.contains("protocol")) {  // 检查每个规则是否包含必要字段
                rules.push_back({  // 将规则添加到向量中
                    rule["name"].get<std::string>(),  // 规则名称
                    rule["listen"].get<std::string>(),  // 监听地址和端口
                    rule["target"].get<std::string>(),  // 目标地址和端口
                    rule["protocol"].get<std::string>()  // 协议类型
                    });
            }
            else {
                Log("Invalid rule format in config file--配置文件读取错误.");  // 记录规则格式错误的情况
            }
        }
    }
    else {
        Log("No valid forward_rules found in config file.");  // 记录配置文件中没有找到有效转发规则的情况
    }

    // 根据读取的转发规则启动相应的转发服务
    for (const auto& rule : rules) {
        StartForwarding(rule);
    }

    Log("Port forwarder running. Press Enter to exit...");  // 记录端口转发器正在运行
    std::cin.get();  // 等待用户输入以退出程序

    logWorkerRunning = false;  // 设置日志工作线程的运行标志为false
    WSACleanup();  // 清理Winsock库
    return 0;  // 返回成功码
}

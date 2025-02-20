#include <string>
#include <iostream>
#include "conlog.h"

void SeparateIpAndPort_listen(const std::string& address, std::string& listen_Address, std::string& listen_port) {
    // 从字符串末尾开始查找冒号，以找到地址和端口之间的分隔符
    size_t colonPos = address.rfind(':');

    // 检查是否找到了冒号，并且确保冒号后面还有字符（即端口号部分）
    if (colonPos != std::string::npos && address.find(':', colonPos + 1) == std::string::npos) {
        listen_Address = address.substr(0, colonPos);
        listen_port = address.substr(colonPos + 1);
    }
    else {
        // 处理错误情况，例如地址中没有有效的冒号分隔或格式不正确
        std::cerr << "Invalid address format: " << address << std::endl;
    }
}

void SeparateIpAndPort_target(const std::string& address, std::string& target_Address, std::string& target_port) {
    // 从字符串末尾开始查找冒号，以找到地址和端口之间的分隔符
    size_t colonPos = address.rfind(':');

    // 检查是否找到了冒号，并且确保冒号后面还有字符（即端口号部分）
    if (colonPos != std::string::npos && address.find(':', colonPos + 1) == std::string::npos) {
        target_Address = address.substr(0, colonPos);
        target_port = address.substr(colonPos + 1);
    }
    else {
        // 处理错误情况，例如地址中没有有效的冒号分隔或格式不正确
        std::cerr << "Invalid address format: " << address << std::endl;
    }
}


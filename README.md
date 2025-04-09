# IPv64-Forwarder
一个简单的程序，它实现基于IPv4的TCP端口或者UDP端口转发（目前仅支持单向UDP）

它属于https://github.com/HyperSharkawa/IPvX-PortForwarder  （基于C#）的简单重置版本，使用C++完成

conlog.h文件基于C#重写为C++，并且加入额外功能

您可以尝试同时运行多个程序（在不同的工作目录下，以免冲突），后续将会加入多线程支持

使用json作为配置文件
---

```json
  {
    "forward_rules": [
    {
            "listen": "监听的IP地址:端口号", 
            "name": "example_rule",
            "protocol": "转发协议--tcp/udp",
            "target": "目标IP地址:端口号"
        }
    ]
  }
```
- **未来计划**：  
  ✓ IPv6支持  
  ✓ 基本异常处理与调试日志  
  √ IPv6与IPv4相互转换  
  ✗ UDP双向转发支持  
  
by Hikarier_Kittens

# IP64-Address&PortForwarder
一个简单的程序，它实现基于IPv4的TCP端口或者UDP端口转发（目前仅支持单向UDP）

您可以尝试同时运行多个程序，后续将会加入多线程支持

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

未来计划：UDP双向转发支持，IPv6支持，IPv6与IPv4相互转换支持

by Hikarier_Kittens

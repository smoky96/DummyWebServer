# DummyWebServer

> 一个简易的 Web 服务器，仅用于学习

一个简单的、带有详细注释的简易 Web 服务器，目前只支持 GET 方法，可以用自己的网站来测试（千万别投入实际使用，会有重大安全漏洞）

## Getting Started

### Prerequisites 项目使用条件

我的测试环境为：

* Ubuntu 20.04
* g++ 9.3.0
* GNU make 4.2.1

## Installation 安装

依次执行以下命令即可完成对 server、cgi、stress 三个程序的编译，编译好的程序在 bin 目录下

```sh
$ git clone https://github.com/smoky96/DummyWebServer.git
$ cd DummyWebServer

$ mkdir bin
$ make
```

## Usage example 使用示例

### server 程序

该程序为服务器主程序，可以接收 HTTP 请求，并返回请求的文件。目前仅支持 GET 与 POST 方法，且 GET 请求的参数会被忽略（因为我不知道要怎么处理这个参数，也不知道这个参数有什么用），POST 是硬编码的，只支持代码中 root 文件夹下的网站。使用了 Proactor 并发模型、线程池、非阻塞 I/O 与 I/O 复用、数据库连接池等技术。

目前可供选择的参数在 include/dummy_server.h 中的 Config 类中查看，有：

* port: 端口号（默认为 8080）
* thread_num: 线程数（默认为 8）
* triger_mode: Epoll 触发模式（默认为 ET）
* sql_user: 数据库用户名
* sql_passwd: 数据库密码
* db_nmae: 数据库名称
* sql_num: 连接池大小

可以自己更改源代码以设置参数，注意使用前更改 src/server/server.cpp 文件中 main 函数的 root 变量，请改为自己的网站根目录，然后重新编译程序。

输入 ```bin/server``` 运行 web 服务器程序

### cgi 程序

该程序为简易的 CGI 程序，可接收请求程序的文件名，然后服务器执行程序后将结果返回给客户端。目前还未集成到服务器主程序中。使用了 Reactor 并发模型、进程池，以及 I/O 复用与非阻塞 I/O 等技术

输入 ```bin/cgi ip_address port_num``` 来运行 CGI 程序，其中：

* ip_address 为本机 ip 地址
* port_num 为端口号

### stress 程序

该程序为服务器压力测试程序，可用来测试服务器的并发性能，采用 I/O 复用技术，让多个 socket 不停的去发起请求。

输入 ```bin/stress ip_address port_num connection_number``` 来运行 stress 程序，其中：

* ip_address 为本机 ip 地址
* port_num 为端口号
* connection_number 为 socket 连接数量（最大值依系统参数而定，一般为 1021）

## History 版本历史

* 2020.05.26
  * 完成程序基本架构
  * 支持 GET 请求
  * 完成 CGI 程序
  * 完成压力测试程序
* 2020.06.12
  * 增加了 URL 编码与解码，现在支持中文路径了
  * 加入了 ET 与 LT 两种触发模式的选择
  * 现已支持 POST 方法（都是硬编码的，只能支持代码文件中 root 目录下的网站，该网站从 [@qinguoyi](https://github.com/qinguoyi/TinyWebServer) 的代码中拷贝下来进行了稍微的修改）
  * 目前 GET 方法会忽略其附带参数

## To-Do

- [x] 加入 POST 方法
- [x] 提供 epoll 的 ET 与 LT 两种触发模式的选择
- [ ] 提供 Reactor 与 Proactor 两种并发模型的选择
- [x] 加入数据库支持
- [ ] 增加日志系统
- [ ] 用定时器处理非活动连接

## Authors 关于作者

* [GuoDong](https://guodong.plus)

## Acknowledgement 致谢

* 主要参考游双所著的《Linux 高性能服务器编程》
* 同时参考了 [@qinguoyi](https://github.com/qinguoyi/TinyWebServer) 所编写的 TinyWebServer 感谢大佬！

## License 授权协议

[GNU General Public License v3.0](https://github.com/smoky96/DummyWebServer/blob/master/LICENSE)

# Have Fun ! d=====(￣▽￣*)b


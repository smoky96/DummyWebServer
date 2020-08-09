# DummyWebServer

> 一个简易的 Web 服务器，仅用于学习

一个简单的、带有详细注释的简易 Web 服务器，目前只支持 GET 和 POST 方法，可以用自己的网站来测试（完整支持 GET 方法，POST 方法是硬编码的）

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

使用前请先在服务器中建表（确保数据库中有 user 表，表中有 username 列和 passwd 列），运行此程序前请先运行 cgi 程序。该 server 程序为服务器主程序，可以接收 HTTP 请求，并返回请求的文件。目前仅支持 GET 与 POST 方法，且 GET 请求的参数会被忽略，POST 是硬编码的，只支持代码中 root 文件夹下的网站。使用了 Proactor 并发模型、线程池、非阻塞 I/O 与 I/O 复用、数据库连接池等技术。

目前可供选择的参数在 include/dummy_server.h 中的 Config 类中查看，也可以输入 ```bin/server``` 查看参数及使用方法：

| 参数             | 说明                             |
| ---------------- | -------------------------------- |
| -u\|--user       | MySQL数据库用户名                |
| -p\|--passwd     | MySQL 数据库密码                 |
| -d\|--db_name    | MySQL 数据库名称                 |
| -s\|--sql_num    | 数据库连接池大小                 |
| -P\|--port       | 端口号                           |
| -t\|--thread_num | 线程数                           |
| -T\|--trigger    | Epoll 触发模式，0 为 ET，1 为 LT |

注意使用前更改 src/server/http_conn.cpp 文件中 doc_root 变量，请改为自己的网站根目录，然后重新编译程序（默认使用 root 目录中的网站）。

### cgi 程序

该程序为简易的 CGI 程序，可接收 python 源码，在服务端执行后将结果返回给客户端。使用了 Reactor 并发模型、进程池，以及 I/O 复用与非阻塞 I/O 等技术

输入 ```bin/cgi ip_address port_num``` 来运行 CGI 程序，其中：

* ip_address 为本机 ip 地址
* port_num 为端口号

### stress 程序

该程序为服务器压力测试程序，可用来测试服务器的并发性能，采用 I/O 复用技术，让多个 socket 不停的去发起请求。

输入 ```bin/stress hostname port_num connection_number time(sec)``` 来运行 stress 程序，其中：

* hostname 为请求的完整 url，如：http://www.website.com/ ，只支持 http 协议
* port_num 为端口号
* connection_number 为 socket 连接数量（最大值依系统参数而定，一般为 1021）
* time 为请求持续时间，单位是秒

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
* 2020.07.26
  * 重写了 stress 程序，现在同时支持使用域名和 ip 地址进行测试
  * 封装了 server 程序的参数处理，现在使用 server 程序需提供参数
  * 重写了 CGI 程序，成为了一个在线 Python 解释器 CGI
  * 加入了在线 Python 解释器页面
* 2020.08.01
  * 加入定时器处理非活动连接，可在 common.h 文件中更改 TIMEOUT 宏定义来自定义超时时间
  * 修复 keep-alive 为 close 时，客户端无法继续读取数据的 bug
* 2020.08.09
  * 用 Bootstrap 简单的重写了 root 下的网站，现在界面漂亮一些了哦
  * 加入 Http Range 字段的支持！现在可以断点续传了！（不过一个请求的 Range 字段只能请求一段，不能一次性请求多段，比如：Range: 0-100,200-400 还不能支持）
  * 超过 10MB 的文件请求会进行分批传输（一次最多响应 10MB 数据给客户端）

## To-Do

- [x] 加入 POST 方法
- [x] 提供 epoll 的 ET 与 LT 两种触发模式的选择
- [ ] 提供 Reactor 与 Proactor 两种并发模型的选择
- [x] 加入数据库支持
- [ ] 增加日志系统
- [x] 用定时器处理非活动连接
- [x] 整合 CGI 程序，加入在线 Python 解释器
- [ ] 线程池动态调整大小
- [x] 加入 Http Range 字段，支持断点续传
- [ ] 将静态文件进行缓存

## Authors 关于作者

* [GuoDong](https://guodong.plus)

## Acknowledgement 致谢

* 主要参考游双所著的《Linux 高性能服务器编程》
* 同时参考了 [@qinguoyi](https://github.com/qinguoyi/TinyWebServer) 所编写的 TinyWebServer 感谢大佬！

## License 授权协议

[GNU General Public License v3.0](https://github.com/smoky96/DummyWebServer/blob/master/LICENSE)

# Have Fun ! d=====(￣▽￣*)b


压力测试
=====
通过ab工具分别压测nginx swoole node.js golang的http server，并观察结果。
web server都是输出一行It work!
硬件环境是一台8G/4核酷睿I5CPU的笔记本电脑，型号是Thinkpad T430.

```
Nginx   ab -c 100 -n 100000 http://localhost/index.html
Swoole  ab -c 100 -n 100000 http://127.0.0.1:8848/
Node.js ab -c 100 -n 100000 http://127.0.0.1:8080/
Golang  ab -c 100 -n 100000 http://127.0.0.1:8080/
```

本次测试使用的软件版本如下：
```
nginx version: nginx/1.2.6 (Ubuntu)
go version go1.1.1 linux/amd64
swoole-1.5.4
node.js-0.11.3-pre
```

代码在./code目录中。

QPS对比
-----
```
Nginx:      Requests per second:    23770.74 [#/sec] (mean)
Golang:     Requests per second:    21807.00 [#/sec] (mean)
Swoole:     Requests per second:    19711.22 [#/sec] (mean)
Node.js:    Requests per second:    6680.53 [#/sec] (mean)
```

内存占用对比
-----
Golang 运行多次压测后内存从2920K上升至5580K，再继续压测不会上升

Node.js运行多次后内存一直在涨，怀疑有轻微内存泄露。从开始运行的5930K，到最后的6060K。

Nginx的4个worker进程，内存占用一直稳定在820K。

Swoole的主进程内存占用一直稳定在3200K，多次压测内存占用没有任何增加。Worker进程的内存有小幅增加。

通过设置Swoole的max_request参数，worker进程的生命周期是可以控制的，生命周期结束后会自动回收所有内存，所以轻微的内存泄露问题也不大。

TCP长连接的维持能力
-----
Nginx、Golang、Swoole、node.js都是使用epoll/kqueue作为事件轮询机制的。维持多少长连接与程序代码本身没有任何关系，取决于操作系统的内存大小。


结果评价
-----
Nginx、Golang、Swoole都是多线程Reactor的，可以充分利用多核，所以成绩是node.js的数倍。
Swoole中的PHP代码需要编译为opcode来执行，没条opcode都是一此函数调用。语言的执行效率效率比C语言（Nginx）,Golang这种编译型的语言差一些。
Node.js的http模块不是多线程的，无法利用多核，结果最差。这里并不是说node.js的性能差，使用第三方的node扩展cluter也可以使node.js变成多进程。



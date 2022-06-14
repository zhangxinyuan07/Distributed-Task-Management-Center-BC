#ifndef WORKLOGIC_H
#define WORKLOGIC_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "locker.h"
#include <list>
#include <string>
#include "worker.h"
#include <unordered_map>

class CWorkLogic
{
public:
    // 这些数据由线程池内所有工作线程共同拥有, 共同维护
    static int m_epollfd;                      // 所有socket上的事件都被注册到同一个epoll对象中
    static int m_user_count;                   // 统计用户数量
    int m_sockfd;                              // 当前连接的socket
    string m_cliIP;                            // 客户端IP地址
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小

    CWorkLogic() {}

    ~CWorkLogic() {}

    // 处理客户端请求并响应的入口, 即工作逻辑的入口, 由线程池中的工作线程调用
    void process();
    void init(int sockfd, const sockaddr_in &addr, unordered_map<int, string> &sockfdToAddr); // 初始化新连接
    void close_conn();                                                                        // 关闭连接
    bool Read();                                                                              // 非阻塞读
    bool Write();                                                                             // 非阻塞写
private:
    void init();                      // 初始化连接
    int process_read();               // 解析读入的数据
    bool process_write(int read_ret); // 准备好要回复的数据

    // 下面这组函数被process_write调用以向写缓冲区写入要回复的数据
    bool response_producer(); // 响应生产者, 把生产者推送的任务加入任务队列并把回复消息写入写缓冲区
    bool response_consumer(); // 响应消费者, 从任务队列取出任务消息对应的address并写入写缓冲区

private:
    struct sockaddr_in m_address; // 客户端的IP地址结构体

    unsigned short m_cliPort; // 上面的结构体中的端口号转换成short

    char m_read_buf[READ_BUFFER_SIZE];   // 读缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区

    // 这些数据由线程池内所有工作线程共同拥有, 共同维护
    static CTaskList m_TaskList;
    static CProducerWaiter m_ProducerWaiter;
    static CConsumerWaiter m_ConsumerWaiter;
    static int m_task_list_size;
};

#endif
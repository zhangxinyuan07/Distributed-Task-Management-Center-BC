

#include "server.h"
#include "worker.h"
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <signal.h>

using namespace std;

CTcpServer TcpServer;

CTaskList TaskList;             // 创建任务队列
CProducerWaiter ProducerWaiter; // 创建生产者服务员
CConsumerWaiter ConsumerWaiter; // 创建消费者服务员

// 这是每个连接进来的客户端的信息
struct sockInfo
{
    int fd; // 通信的文件描述符
    struct sockaddr_in addr;
    pthread_t tid; // 线程号
};

struct sockInfo sockinfos[128]; // 最多同时创建128个子线程连接客户端

// 用于与客户端通信的子线程的工作函数
void *working(void *arg);

// 接收TM发来的生产的任务ID, 并进行相应处理
bool process_producer(int cfd, char *comBuffer, string cliIP);

// 接收TM发来的要消费的任务ID, 并进行相应处理
bool process_consumer(int cfd, char *comBuffer);

bool process_sync(int cfd, char *comBuffer);

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("按照如下格式运行 : %s prot_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    // 1. 初始化服务器(socket创建, 绑定, 监听)
    if (TcpServer.InitServer(port) == false)
    {
        printf("服务端初始化失败，程序退出。\n");
        return -1;
    }

    // 初始化数据
    int max = sizeof(sockinfos) / sizeof(sockinfos[0]);
    for (int i = 0; i < max; i++)
    {
        bzero(&sockinfos[i], sizeof(sockinfos[i]));
        sockinfos[i].fd = -1;
        sockinfos[i].tid = -1;
    }

    // 不断循环等待客户端连接，一旦一个客户端连接进来，就创建一个子线程进行通信
    while (1)
    {
        struct sockaddr_in cliaddr;
        int len = sizeof(cliaddr);
        // 接受连接
        int cfd = accept(TcpServer.m_listenfd, (struct sockaddr *)&cliaddr, (socklen_t *)&len);

        struct sockInfo *pinfo;
        for (int i = 0; i < max; i++)
        {
            // 从这个数组中找到一个可以用的sockInfo元素
            if (sockinfos[i].fd == -1)
            {
                pinfo = &sockinfos[i];
                break;
            }
            if (i == max - 1)
            {
                sleep(1);
                i--;
            }
        }

        // 把客户端信息赋值给结构体
        pinfo->fd = cfd;
        memcpy(&pinfo->addr, &cliaddr, len);

        // 创建子线程
        pthread_create(&pinfo->tid, NULL, working, pinfo);

        pthread_detach(pinfo->tid);
    }

    TcpServer.CloseListen();

    return 0;
}

void *working(void *arg)
{
    struct sockInfo *pinfo = (struct sockInfo *)arg;

    char cliIp[16];
    inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, cliIp, sizeof(cliIp));
    unsigned short cliPort = ntohs(pinfo->addr.sin_port);
    printf("客户端IP : %s, 端口 : %d\n", cliIp, cliPort);
    string clientIP(cliIp);

    char comBuffer[1024];

    while (1)
    {
        // sleep(1);
        memset(comBuffer, 0, sizeof(comBuffer) / sizeof(char));

        // 接收数据
        if (TcpServer.Recv(pinfo->fd, comBuffer, sizeof(comBuffer)) <= 0)
            break;
        // printf("接收：%s\n", comBuffer);

        if (comBuffer[0] == '0')
        {
            // 处理生产任务
            if (process_producer(pinfo->fd, comBuffer, clientIP) == false)
                break;
        }
        else if (comBuffer[0] == '1')
        {
            // 处理消费任务
            if (process_consumer(pinfo->fd, comBuffer) == false)
                break;
        }
        else if (comBuffer[0] == 's')
        {
            if (process_sync(pinfo->fd, comBuffer) == false)
            {
                break;
            }
        }
        else
        {
            // 格式有误
            sprintf(comBuffer, "客户端数据格式错误, 请重新发送...\n");
            if (TcpServer.Send(pinfo->fd, comBuffer, strlen(comBuffer) + 1) <= 0)
                break;
        }
    }

    printf("客户端已断开连接...\n");
    close(pinfo->fd);
    return NULL;
}

bool process_producer(int cfd, char *comBuffer, string clientIP)
{

    // 接收到一个任务消息: “0,332652,5000”
    // 从中提取出任务ID, 把任务加到任务队列里去
    // 从下标为2的字符开始, 到第二个逗号前面一个字符结束, 这部分内容为任务ID
    // 从第二个逗号后面一个字符开始, 到换行符前面一个字符结束, 这部分内容为该TM监听客户端连接的端口号
    int taskID_len = 0; // 任务ID的位数
    for (int i = 2; i < strlen(comBuffer); i++)
    {
        if (comBuffer[i] == ',')
        {
            break;
        }
        else
        {
            taskID_len++;
        }
    }

    string temp(comBuffer + 2, comBuffer + 2 + taskID_len);
    int taskID = stoi(temp);                                                      // 任务ID提取完毕
    string cli_listen_port_ID(comBuffer + 2 + taskID_len + 1, strlen(comBuffer)); // TM监听端口号提取完毕
    string cliAddr = "";
    cliAddr += clientIP;
    cliAddr += ",";
    cliAddr += cli_listen_port_ID;
    ProducerWaiter.add_task(TaskList, taskID, cliAddr); // 向当前连接的任务管理器所对应的任务列表里添加任务
    cout << "已收到任务ID : " << taskID << endl;

    // 回复TM一个确认消息
    sprintf(comBuffer, "a");
    if (TcpServer.Send(cfd, comBuffer, strlen(comBuffer)) <= 0)
    {
        return false;
    }
    return true;
}

bool process_consumer(int cfd, char *comBuffer)
{

    int taskID_len = 0; // 任务ID的位数
    for (int i = 2; i < strlen(comBuffer); i++)
    {
        if (comBuffer[i] == ',')
        {
            break;
        }
        else
        {
            taskID_len++;
        }
    }
    string temp(comBuffer + 2, comBuffer + 2 + taskID_len);
    int taskID = stoi(temp); // 任务ID提取完毕

    string taskAddr = ConsumerWaiter.del_task(TaskList, taskID);
    sprintf(comBuffer, "%s", taskAddr.c_str());
    if (TcpServer.Send(cfd, comBuffer, strlen(comBuffer) + 1) <= 0)
        return false;
    cout << "已向TM发送任务地址 : " << taskAddr << endl;
    return true;
}

bool process_sync(int cfd, char *comBuffer)
{
    // 有点问题
    // 

    // vector<int> tasklist = TaskList.get_all_tasks();
    // string ret = "";
    // for (int &taskID : tasklist)
    // {
    //     ret += to_string(taskID);
    //     ret += ", ";
    // }
    // // 这里用sprintf向comBuffer写入ret，可能会导致溢出
    // // 使用snprintf函数可截断超出范围的部分
    // printf("ret : %s\n", ret.c_str());
    // sprintf(comBuffer, "%s", ret.c_str());
    // printf("发送 : %s\n", comBuffer);
    // if (TcpServer.Send(cfd, comBuffer, strlen(comBuffer)) <= 0)
    //     return false;
    return true;
}
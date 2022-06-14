// 任务管理器
#include "server.h"
#include "worker.h"
#include "client.h"
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

CTcpServer TcpServer;

CTcpClient TcpClient_get_TM_address_from_TMC;  // 用于连接TMC，获取任务内容存放的地址
CTcpClient TcpClient_get_task_content_from_TM; // 用于连接其他TM，获取任务内容
CTcpClient TcpClient_send_task_ID_to_TMC;      // 用于连接TMC, 把生产到本TM的任务ID发送给TMC, 并且等待TMC返回确认消息

CTaskList TaskList;             // 创建任务队列
CProducerWaiter ProducerWaiter; // 创建生产者服务员

// 这是每个连接进来的客户端的信息
struct sockInfo
{
    int fd; // 通信的文件描述符
    struct sockaddr_in addr;
    pthread_t tid; // 线程号
};

struct sockInfo sockinfos[128]; // 最多同时创建128个子线程连接客户端

int port;                // 监听的端口号
string IP = "127.0.0.1"; // 自己的IP地址, 这个需要根据情况有改动

FILE *fp; // 本地任务文件

// 用于与客户端通信的子线程的工作函数
void *working(void *arg);

// 与生产者通信的内容
bool process_producer(int cfd, char *comBuffer);

// 与消费者通信的内容
bool process_consumer(int cfd, char *comBuffer);

// 与其他任务管理器进程通信的内容
void process_TM(int cfd, char *comBuffer);

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("按照如下格式运行 : %s prot_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    port = atoi(argv[1]);

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

    // 初始化需要的连接

    TcpClient_get_TM_address_from_TMC.InitClient();
    TcpClient_get_TM_address_from_TMC.Connect(10000);

    TcpClient_send_task_ID_to_TMC.InitClient();
    TcpClient_send_task_ID_to_TMC.Connect(10000);

    // 打开任务文件, 不存在则创建一个新的文件
    fp = fopen("tasklist.txt", "a+");
    char line[1000];
    if (fp == NULL)
    {
        puts("File open error");
    }

    // 逐行读取数据
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if (strlen(line) != 0)
        {
            // 如果读取到数据，就解析内容并把任务添加到任务列表里
            printf("已从备份加载到任务数据 : %s", line);
            // 解析任务ID
            int taskID_len = 0; // 任务ID的位数
            for (int i = 0; i < strlen(line); i++)
            {
                if (line[i] == ',')
                {
                    break;
                }
                else
                {
                    taskID_len++;
                }
            }
            string temp(line, line + taskID_len);
            int taskID = stoi(temp); // 任务ID提取完毕
            // 解析任务Content
            string taskContent(line + taskID_len + 2, line + strlen(line) - 1);
            ProducerWaiter.add_task(TaskList, taskID, taskContent); // 添加任务
        }
    }
    fclose(fp);

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
    fclose(fp);

    return 0;
}

void *working(void *arg)
{

    struct sockInfo *pinfo = (struct sockInfo *)arg;

    char cliIp[16];
    inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, cliIp, sizeof(cliIp));
    unsigned short cliPort = ntohs(pinfo->addr.sin_port);
    printf("客户端IP : %s, 端口 : %d\n", cliIp, cliPort);

    // 与客户端通信
    char comBuffer[1024];

    while (1)
    {
        // sleep(1);
        memset(comBuffer, 0, sizeof(comBuffer) / sizeof(char));

        // 接收数据
        if (TcpServer.Recv(pinfo->fd, comBuffer, sizeof(comBuffer)) <= 0)
            break;
        // printf("接收：%s\n", comBuffer);

        // 把接收到的数据转交给生产者或消费者去处理
        // comBuffer的第0位用来判断是生产者('1')还是消费者('0')
        if (comBuffer[0] == '0')
        {
            // 转交给生产者服务员
            if (process_producer(pinfo->fd, comBuffer) == false)
                break;
        }
        else if (comBuffer[0] == '1')
        {
            // 转交给消费者服务员
            if (process_consumer(pinfo->fd, comBuffer) == false)
                break;
        }
        else if (comBuffer[0] == '2')
        {
            process_TM(pinfo->fd, comBuffer);
            break; // 处理完了就断开连接
        }
        else
        {
            // 格式有误, 无法识别该发送给消费者还是生产者
            sprintf(comBuffer, "客户端数据格式错误, 请重新发送...\n");
            if (TcpServer.Send(pinfo->fd, comBuffer, strlen(comBuffer) + 1) <= 0)
                break;
        }
    }

    printf("客户端已断开连接...\n");
    close(pinfo->fd);
    return NULL;
}

void process_TM(int cfd, char *comBuffer)
{
    // 解析任务ID
    int taskID_len = 0; // 任务ID的位数
    string temp(comBuffer + 2, comBuffer + strlen(comBuffer));
    int taskID = stoi(temp); // 任务ID提取完毕

    sprintf(comBuffer, "%s", TaskList.m_task_list[taskID].c_str());
    TcpServer.Send(cfd, comBuffer, strlen(comBuffer));
}

bool process_producer(int cfd, char *comBuffer)
{
    // 解析任务ID
    int taskID_len = 0; // 任务ID的位数
    for (int i = 2; i < strlen(comBuffer); i++)
    {
        if (comBuffer[i] == '\n')
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

    // 与TMC通信, 等待TMC在其任务列表中添加好ID和对应的地址
    char strBuffer[1024];
    memset(strBuffer, 0, sizeof(strBuffer));
    sprintf(strBuffer, "0,%s,%s", to_string(taskID).c_str(), to_string(port).c_str());
    TcpClient_send_task_ID_to_TMC.Send(strBuffer, strlen(strBuffer));
    TcpClient_send_task_ID_to_TMC.Recv(strBuffer, sizeof(strBuffer));

    // 如果TMC添加成功,返回"a",则在本地任务列表添加一对任务ID和任务Content的映射
    // 否则给Client返回 "任务XXX添加失败"
    if (strBuffer[0] == 'a')
    {
        // 解析任务Content
        int task_content_len = 0;
        for (int i = 2 + taskID_len + 1; i < strlen(comBuffer); i++)
        {
            task_content_len++;
        }
        string taskContent(comBuffer + 2 + taskID_len + 1, comBuffer + 2 + taskID_len + 1 + task_content_len);

        ProducerWaiter.add_task(TaskList, taskID, taskContent); // 生产任务

        // 向文件中添加日志(AOF方法实现持久化)
        fp = fopen("tasklist.txt", "a+");
        fprintf(fp, "%d,%s\n", taskID, taskContent.c_str());
        fclose(fp);
        cout << "已收到任务ID : " << taskID << endl;

        sprintf(comBuffer, "已接收生产者任务: %d\n", taskID);
    }
    else
    {
        sprintf(comBuffer, "任务%d添加失败\n", taskID);
    }

    if (TcpServer.Send(cfd, comBuffer, strlen(comBuffer)) <= 0)
        return false;

    return true;
}

bool process_consumer(int cfd, char *comBuffer)
{
    // 解析任务ID
    string temp(comBuffer + 2, comBuffer + strlen(comBuffer));
    int taskID = stoi(temp); // 任务ID提取完毕

    char strBuffer[1024];
    memset(strBuffer, 0, sizeof(strBuffer));
    strcpy(strBuffer, comBuffer);

    // 与TMC通信
    TcpClient_get_TM_address_from_TMC.Send(strBuffer, strlen(strBuffer));
    TcpClient_get_TM_address_from_TMC.Recv(strBuffer, sizeof(strBuffer));

    printf("收到TMC返回的地址信息为: %s\n", strBuffer);

    if (!isdigit(strBuffer[0]))
    {
        sprintf(comBuffer, "任务ID : %d 已被消费\n", taskID);
        if (TcpServer.Send(cfd, comBuffer, strlen(comBuffer)) <= 0)
            return false;
        return true;
    }
    else
    {
        int IP_len = 0;
        for (int i = 0; i < strlen(strBuffer); i++)
        {
            if (strBuffer[i] != ',')
            {
                IP_len++;
            }
            else
            {
                break;
            }
        }
        string conn_IP(strBuffer, strBuffer + IP_len); // 解析出存放想消费的任务内容存放的目的TM的地址
        string tmp(strBuffer + IP_len + 1, strlen(strBuffer));
        int conn_port = stoi(tmp);
        if (conn_port == port && conn_IP == IP)
        {
            // 如果发现目的TM就是自己, 直接写入
            sprintf(comBuffer, "%s", TaskList.m_task_list[taskID].c_str());
        }
        else
        {
            // 不是自己, 就根据相应的IP和端口号去连接
            TcpClient_get_task_content_from_TM.InitClient();
            TcpClient_get_task_content_from_TM.Connect(conn_port, conn_IP);
            memset(strBuffer, 0, sizeof(strBuffer));
            sprintf(strBuffer, "2,%s", to_string(taskID).c_str());
            TcpClient_get_task_content_from_TM.Send(strBuffer, strlen(strBuffer));
            TcpClient_get_task_content_from_TM.Recv(strBuffer, sizeof(strBuffer));
            TcpClient_get_task_content_from_TM.CloseClient();
            printf("收到返回的任务内容为 : %s\n", strBuffer);

            memset(comBuffer, 0, sizeof(comBuffer));
            strcpy(comBuffer, strBuffer);
        }
        TcpServer.Send(cfd, comBuffer, strlen(comBuffer));
    }

    return true;
}
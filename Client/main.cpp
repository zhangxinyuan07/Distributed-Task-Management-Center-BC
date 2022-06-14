// 客户端

#include "client.h"
#include <iostream>
using namespace std;

CTcpClient TcpClient; // 创建一个TCP客户端

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("按该格式运行 : %s role_number(0 : producer, 1 : consumer) port_number\n", basename(argv[0]));
        exit(-1);
    }

    // 获取角色信息, 0代表生产者, 1代表消费者
    int role = atoi(argv[1]);
    int port = atoi(argv[2]);

    int produce_start_ID = 0; // 生产任务的起始ID
    int consume_start_ID = 0; // 消费任务的起始ID
    int consumerMode = 0;     // 消费任务的模式（轮询 OR 随机挑选）

    if (role == 0)
    {
        printf("生产者已启动, 请输入生产任务ID的整型初始值: \n");
        cin >> produce_start_ID;
    }
    else if (role == 1)
    {
        printf("消费者已启动, 轮询任务(任务ID起始值) or 随机挑选任务？(-1) \n");
        cin >> consumerMode;
    }
    else
    {
        printf("程序启动失败: 角色信息错误. 0为生产者, 1为消费者.\n");
        exit(-1);
    }

    // 1. 创建客户端socket
    if (TcpClient.InitClient() == false)
    {
        printf("客户端初始化失败，程序退出...\n");
        return -1;
    }

    // 2. 连接TM
    if (TcpClient.Connect(port) == false)
    {
        printf("连接服务器失败，程序退出...\n");
        return -1;
    }

    // 3. 与服务器通信
    char strbuffer[1024];
    if (consumerMode == -1)
    {
        while (true)
        {
            // 随机挑选模式，先从TM获取当前可消费的任务ID
            // sprintf(strbuffer, "s");
            // TcpClient.Send(strbuffer, strlen(strbuffer));

            // TcpClient.Recv(strbuffer, sizeof(strbuffer));
            // printf("当前可消费任务ID如下: \n");
            // printf("%s\n", strbuffer);

            printf("请输入要消费的ID: ");
            cin >> consume_start_ID;
            sprintf(strbuffer, "1,%d", consume_start_ID);
            TcpClient.Send(strbuffer, strlen(strbuffer));
            TcpClient.Recv(strbuffer, sizeof(strbuffer));
            printf("接收：%s\n", strbuffer);
        }
    }

    // 轮询模式
    consume_start_ID = consumerMode;
    while (true)
    {

        memset(strbuffer, 0, sizeof(strbuffer));

        if (role == 0)
        {
            // 第1位代表是生产者, 第七位到换行符之前为任务ID
            sprintf(strbuffer, "0,%d\n任务内容%d", produce_start_ID, produce_start_ID);
            produce_start_ID++;
        }
        else if (role == 1)
        {
            sprintf(strbuffer, "1,%d", consume_start_ID);
            consume_start_ID++;
        }

        if (TcpClient.Send(strbuffer, strlen(strbuffer)) <= 0)
            break;
        // printf("发送：%s\n", strbuffer);

        if (TcpClient.Recv(strbuffer, sizeof(strbuffer)) <= 0)
            break;
        printf("接收：%s\n", strbuffer);

        sleep(1);
    }
    printf("服务器已断开连接...\n");
    return 0;
}
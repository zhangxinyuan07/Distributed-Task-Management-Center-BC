#include "worklogic.h"

int CWorkLogic::m_epollfd = -1;
int CWorkLogic::m_user_count = 0;

CTaskList CWorkLogic::m_TaskList;
CProducerWaiter CWorkLogic::m_ProducerWaiter;
CConsumerWaiter CWorkLogic::m_ConsumerWaiter;
// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 向epoll对象添加fd,监听fd上的读事件
void addfd_to_epoll(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // EPOLLRDHUP可以检测对方连接断开, 监听读事件
    event.events = EPOLLIN | EPOLLRDHUP;
    // 注册EPOLLONESHOT事件, 以保证一个socket连接在任一时刻都只被一个线程处理
    // 每次该事件触发, 在当前socket处理完后, 还要及时重置该事件, 以保证下次还能触发
    if (one_shot)
    {
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll对象中移除fd
void rmfd_from_epoll(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改epoll中文件描述符fd监听的事件为ev
void modfd_in_epoll(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    // 重置一下EPOLLONESHOT和EPOLLRDHUP事件
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void CWorkLogic::init(int sockfd, const sockaddr_in &addr, unordered_map<int, string> &sockfdToAddr)
{
    // 通信sockfd的信息传递进来
    m_sockfd = sockfd;
    m_address = addr;

    char cliIP[16];
    // 解析客户端IP地址信息
    inet_ntop(AF_INET, &m_address.sin_addr.s_addr, cliIP, sizeof(cliIP));
    m_cliIP = cliIP;
    m_cliPort = ntohs(m_address.sin_port);
    sockfdToAddr[sockfd] = m_cliIP; // debug用
    cout << "新连接的客户端IP: " << m_cliIP << " 端口: " << m_cliPort << endl;

    // // 设置客户端连接进来的sockfd端口复用
    // int reuse = 1;
    // setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 把通信sockfd添加到epoll对象中
    addfd_to_epoll(m_epollfd, m_sockfd, true);
    m_user_count++; // 用户数+1

    init();
}

void CWorkLogic::init()
{
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
}

void CWorkLogic::close_conn()
{
    if (m_sockfd != -1)
    {
        rmfd_from_epoll(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
        printf("客户端连接已断开...\n");
    }
}

int CWorkLogic::process_read()
{
    // 第一位是0代表是生产者, 1代表消费者
    if (m_read_buf[0] == '0')
    {
        return 0;
    }
    else if (m_read_buf[0] == '1')
    {
        return 1;
    }
    else
    {
        return -1;
    }
}

bool CWorkLogic::response_producer()
{
    // 接收到一个任务消息: “0,332652,5000”
    // 从中提取出任务ID, 把任务加到任务队列里去
    // 从下标为2的字符开始, 到第二个逗号前面一个字符结束, 这部分内容为任务ID
    // 从第二个逗号后面一个字符开始, 到换行符前面一个字符结束, 这部分内容为该TM监听客户端连接的端口号
    int taskID_len = 0; // 任务ID的位数
    for (int i = 2; i < strlen(m_read_buf); i++)
    {
        if (m_read_buf[i] == ',')
        {
            break;
        }
        else
        {
            taskID_len++;
        }
    }

    string temp(m_read_buf + 2, m_read_buf + 2 + taskID_len);
    int taskID = stoi(temp);                                                        // 任务ID提取完毕
    string cli_listen_port_ID(m_read_buf + 2 + taskID_len + 1, strlen(m_read_buf)); // TM监听端口号提取完毕
    string cliAddr = "";
    cliAddr += m_cliIP;
    cliAddr += ",";
    cliAddr += cli_listen_port_ID;
    bool ret = m_ProducerWaiter.add_task(m_TaskList, taskID, cliAddr); // 向当前连接的任务管理器所对应的任务列表里添加任务
    if (ret) {
        cout << "已收到任务ID : " << taskID << endl;
        cout << "已添加任务地址 : " << cliAddr << endl;

        // 回复TM一个确认消息
        sprintf(m_write_buf, "a, 添加成功");
    } else {
        cout << "任务ID : " << taskID << " 已存在, 不能重复添加" << endl;
        sprintf(m_write_buf, "任务ID已存在, 添加失败");
    }
    
    return true;
}

bool CWorkLogic::response_consumer()
{
    int taskID_len = 0; // 任务ID的位数
    for (int i = 2; i < strlen(m_read_buf); i++)
    {
        if (m_read_buf[i] == ',')
        {
            break;
        }
        else
        {
            taskID_len++;
        }
    }
    string temp(m_read_buf + 2, m_read_buf + 2 + taskID_len);
    int taskID = stoi(temp);                                         // 任务ID提取完毕
    string taskAddr = m_ConsumerWaiter.del_task(m_TaskList, taskID); // 任务地址提取完毕,已消费列表中记录完毕

    // 回复TM一个任务地址
    sprintf(m_write_buf, "%s", taskAddr.c_str());
    cout << "已向TM发送任务地址 : " << taskAddr << endl;
    return true;
}

bool CWorkLogic::process_write(int read_ret)
{
    if (read_ret == 0)
    {
        return response_producer();
    }
    else
    {
        return response_consumer();
    }
}

bool CWorkLogic::Read()
{
    int bytes_read = 0;
    bytes_read = recv(m_sockfd, m_read_buf, sizeof(m_read_buf), 0);
    if (bytes_read == -1)
    {
        perror("recv");
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 没有数据
            printf("读缓冲区无数据...\n");
        }
        return false;
    }
    else if (bytes_read == 0)
    {
        // 对方关闭连接
        printf("对方关闭连接...\n");
        return false;
    }
    return true;
}

bool CWorkLogic::Write()
{
    int temp = 0;

    temp = send(m_sockfd, m_write_buf, strlen(m_write_buf) + 1, 0);
    if (temp <= -1)
    {
        if (errno == EAGAIN)
        {
            modfd_in_epoll(m_epollfd, m_sockfd, EPOLLOUT);
            return true;
        }
        return false;
    }

    modfd_in_epoll(m_epollfd, m_sockfd, EPOLLIN);
    init();
    return true;
}

void CWorkLogic::process()
{
    // cout << "客户端IP: " << m_cliIP << " 端口: " << m_cliPort << endl;

    // 此时已经通过CWorkLogic::write()把客户端发来的数据全部读取放到读缓冲区里了

    // 解析读进来的数据
    int read_ret = process_read();
    if (read_ret == -1)
    {
        // 数据格式错误, 解析失败, 返回-1, 则修改监听文件描述符时间为读事件, 继续监听
        modfd_in_epoll(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 解析完数据以后, 根据解析的结果向写缓冲区里写入相应的回复消息
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        printf("写入数据失败...\n");
        close_conn();
    }

    // 接下来就可以把epoll对象里检测的与客户端通信的文件描述符的事件修改为写事件
    // main函数里就可以调用CWorkLogic::write()把写缓冲区的数据发送给客户端
    modfd_in_epoll(m_epollfd, m_sockfd, EPOLLOUT);
}
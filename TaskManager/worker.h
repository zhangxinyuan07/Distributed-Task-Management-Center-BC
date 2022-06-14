// 定义了一个任务队列和两个角色, 分别为生产者服务员和消费者服务员

#ifndef WORKER_H
#define WORKER_H

#include <vector>
#include <unordered_set>
#include <map>
#include <string>
#include <iostream>

#include "locker.h"

using namespace std;

// 任务队列类, 可操作任务中心, 增删任务
// 生产者服务员和消费者服务员操作该类
// T是任务类型
class CTaskList
{
public:
    CTaskList(){};

    map<int, string> m_task_list;

    CLocker m_lock; // 互斥量, 保护已生产任务列表

    ~CTaskList(){};
};

// 生产者服务员
// 用于处理连接到服务器的生产者客户端的请求, 根据其发来的消息, 向任务队列中增加任务
class CProducerWaiter
{
public:
    CProducerWaiter(){};

    // 把生产者发来的任务加入队列的行为
    // 需要传入一个已生产任务列表
    // 还需要传入一个任务内容(一个全局唯一的字符串)
    // 这里的行为是: 向已生产任务列表里添加一个任务
    void add_task(CTaskList &TaskList, int taskID, string taskContent)
    {
        TaskList.m_lock.lock(); // 加锁

        TaskList.m_task_list.insert(make_pair(taskID, taskContent)); // 写入任务

        TaskList.m_lock.unlock(); // 解锁
    }

    ~CProducerWaiter(){};
};

#endif
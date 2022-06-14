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

    map<int, string> m_meta_task_list;

    unordered_set<int> m_con_list; // 已消费任务列表

    CLocker m_lock; // 互斥量, 保护已生产任务列表

    // 取得当前所有可以消费的任务
    vector<int> get_all_tasks()
    {
        vector<int> ret;
        m_lock.lock();
        for (auto& pair : m_meta_task_list)
        {
            if (m_con_list.count(pair.first) == 0)
            {
                ret.push_back(pair.first);
            }
        }
        m_lock.unlock();
        return ret;
    }

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
    void add_task(CTaskList &TaskList, int taskID, string taskAddr)
    {
        TaskList.m_lock.lock();    // 加锁
        TaskList.m_meta_task_list.insert(make_pair(taskID, taskAddr)); 
        TaskList.m_lock.unlock();  // 解锁
    }

    ~CProducerWaiter(){};
};

// 消费者服务员
// 用于处理连接到服务器的消费者客户端的请求, 根据其发来的消息, 向任务队列中取出任务
class CConsumerWaiter
{
public:
    CConsumerWaiter(){};


    string del_task(CTaskList &TaskList, int taskID)
    {
        string taskAddr;
        TaskList.m_lock.lock();
        if (TaskList.m_con_list.count(taskID) != 0)
            taskAddr = "The task has been consumed...";
        else {
            TaskList.m_con_list.insert(taskID);
            taskAddr = TaskList.m_meta_task_list[taskID];
        }

        TaskList.m_lock.unlock();

        return taskAddr;
    }

    ~CConsumerWaiter(){};
};

#endif
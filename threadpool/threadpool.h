#ifndef LOCKER_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../locker/locker.h" //自定义的锁和信号量封装
#include "../sql/sql_connection_pool.h"//自定义的数据库连接池
template <typename T>// ？？？
class threadpool
{
    public:
    //thread_number 是线程池中线程的数量，max_requests是请求队列中最多允许等待处理的请求数量
    threadpool(int actor_model,connection_pool *connPool,int thread_number = 8,int max_request = 1000);
    ~threadpool();
    bool append(T *request,int state); // ？？？
    bool append_p(T *request);//？？？

    private:
    //
    static void *worker(void *arg); 
    void run();//？？？

    private:
    int m_thread_number; //线程池中的线程数
    int m_max_requests; //请求队列中允许的最大请求数
    pthread_t *m_threads; //储存线程id的数组，储存所有线程池里线程的id

    std::list<T *> m_workqueue; //任务队列
    locker m_queuelocker; //保护请求队列的互斥锁
    sem m_queuestat; //信号量，用于线程间的任务通知，是否有任务需要处理
    connection_pool *m_connPool; //数据库连接池，用于从池中获取数据库连接
    int m_actor_model; //模式标志，用于区分Reactor和Proactor模式

};

template <typename T>
//重写构造函数，用于初始化线程池
threadpool<T>::threadpool(int actor_model,connection_pool *connPool,int thread_number,int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number),m_max_requests(max_requests),m_threads(NULL),m_connPool(connPool)
{
    if(thread_number<=0||max_requests<=0)
      {
        throw std::exception();
      }
        
    m_threads = new pthread_t[m_thread_number];//m_threads是pthread_t*指针类型，相当于存储所有线程池里线程id的数组，现在要给指针（数组）分配空间，空间大小为m_thread_number即线程的数量

    if(!m_threads)//（!m_threads)为真，!m_thread不为0，即m_thread为0
        {
            throw std::exception();//抛出异常
        }
      
      for(int i=0;i<thread_number;i++)//创建线程池里的线程
      {
        if(pthread_create(m_threads+i,NULL,worker,this)!=0)
        {
            delete[] m_threads;//pthread_create创建成功会返回0，如果返回非0值，表示线程创建失败，用delete[]释放已分配的m_thread数组内存
            throw std::exception();
        }
        //pthread_create(pthread_t *,const pthread_attr_t *,void*(start_rtn)(void*),void* arg)
        //第一个参数是事先创建好的pthread_t类型的参数，函数成功创建时第一个参数指向的内存单元被设置为新创建线程的线程id
        //第二个参数用于定制各种不同的线程属性，通常直接设为NULL
        //第三个参数是一个函数类型，表示新创建线程从此函数开始运行
        //第四个参数为第三个函数参数的参数，若第三个参数有参数，第四个参数为输入参数的地址；若无参数，可直接设置为NULL。
        //m_theads+i是线程id的存储地址，表示第i个线程的存储位置。
        //线程属性设置为NULL，使用默认属性
        //worker线程函数，线程创建后会执行worker函数
        //this表示当前线程的地址，将其作为参数传入worker函数
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
        //用pthread_detach将线程设置为分离状态，分离状态的线程会在结束时自动释放资源，无需调用pthread_join
        //如果pthread_detach返回非0值，表示分离失败
      }
}

//重写析构函数
template <typename T>
threadpool<T> ::~threadpool()
{
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)//向线程池的任务队列中添加一个任务，参数是待添加任务对象的指针
{
    m_queuelocker.lock();//上锁
    if(m_workqueue.size() >= m_max_requests)//检查队列是否已满
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);//将当前任务添加到队列
    m_queuelocker.unlock();//释放互斥锁，允许其他线程访问
    m_queuestat.post();//m_queuestat是一个信号量对象，post()会递增信号量的值，唤醒在m_queuestst.wait()处阻塞的线程
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    //thread_create第四个参数即worker函数是线程的入口函数参数，它的类型是接受void*指针，void*指针是一种通用指针，可以指向任意类型数据，用于跨类型数据传递。this指针类型是明确的，指向当前类对象的指针，类型为 类名 *const（常量指针），但是在需要void*参数的时，可以将this赋值给void*，进行隐式转换。但是在后续代码中要显式转回原始类型。
    //worker是thread_create第三个参数，要求类型是void*(*)(void*)，即一个接受void*参数并返回void*的普通函数指针。普通成员函数隐含一个this指针，实际函数签名是void*(std::*)(void*),类型不兼容。所以worker函数得是静态成员函数没有this指针(全局函数也没有)。
    threadpool *pool = (threadpool *)arg;//通过this指针隐式转换成void*类型，输入到worker函数，因为worker函数是静态函数，所以可以接受void*指针，但是隐式转换后要转换回原来的类型，这一步就是转换。转换成threadpool*就可以访问线程池对象的共享资源，比如任务队列，数据库连接池等。
    pool -> run();//???
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
  while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif
  







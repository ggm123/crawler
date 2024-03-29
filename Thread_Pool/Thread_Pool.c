/*
 * Thread_Pool1.c
 *
 *  Created on: 2019-9-18
 *      Author: siasunhebo
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include <Thread_Pool.h>
#include <main.h>
#include <CNS_MGS_160S.h>
#include <SPC_SDIO_1616.h>
#include <CNS_RFID_1S.h>
#include <FD123_CA_000.h>
#include <HC_SuK043_C.h>
#include <can.h>
#include <TCP.h>
#include<Reproducer_Lamplight.h>

/*
*线程池里所有运行和等待的任务都是一个CThread_worker
*由于所有任务都在链表里，所以是一个链表结构
*/
typedef struct worker
{
    /*回调函数，任务运行时会调用此函数，注意也可声明成其它形式*/
    void *(*process) (void *arg);
    void *arg;/*回调函数的参数*/
    struct worker *next;
} CThread_worker;

/*线程池结构*/
typedef struct
{
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_ready;

    /*链表结构，线程池中所有等待任务*/
    CThread_worker *queue_head;

    /*是否销毁线程池*/
    int shutdown;
    pthread_t *threadid;
    /*线程池中允许的活动线程数目*/
    int max_thread_num;
    /*当前等待队列的任务数目*/
    int cur_queue_size;

} CThread_pool;


int pool_add_worker (void *(*process) (void *arg), void *arg);
void *thread_routine (void *arg);


static CThread_pool *pool = NULL;
void pool_init (int max_thread_num)
{
    pool = (CThread_pool *) malloc (sizeof (CThread_pool));

    pthread_mutex_init (&(pool->queue_lock), NULL);
    pthread_cond_init (&(pool->queue_ready), NULL);

    pool->queue_head = NULL;

    pool->max_thread_num = max_thread_num;
    pool->cur_queue_size = 0;

    pool->shutdown = 0;

    pool->threadid = (pthread_t *) malloc (max_thread_num * sizeof (pthread_t));
    int i = 0;
    for (i = 0; i < max_thread_num; i++)
    {
        pthread_create (&(pool->threadid[i]), NULL, thread_routine,NULL);
    }
}



/*向线程池中加入任务*/
int pool_add_worker (void *(*process) (void *arg), void *arg)
{
    /*构造一个新任务*/
    CThread_worker *newworker = (CThread_worker *) malloc (sizeof (CThread_worker));
    newworker->process = process;
    newworker->arg = arg;
    newworker->next = NULL;/*别忘置空*/

    pthread_mutex_lock (&(pool->queue_lock));//阻塞申请互斥锁
    /*将任务加入到等待队列中*/
    CThread_worker *member = pool->queue_head;
    if (member != NULL)
    {
        while (member->next != NULL)
            member = member->next;
        member->next = newworker;
    }
    else
    {
        pool->queue_head = newworker;
    }

    assert (pool->queue_head != NULL);

    pool->cur_queue_size++;
    pthread_mutex_unlock (&(pool->queue_lock));//释放互斥锁
    /*好了，等待队列中有任务了，唤醒一个等待线程；
    注意如果所有线程都在忙碌，这句没有任何作用*/
    pthread_cond_signal (&(pool->queue_ready));
    return 0;
}



/*销毁线程池，等待队列中的任务不会再被执行，但是正在运行的线程会一直
把任务运行完后再退出*/
int pool_destroy ()
{
    if (pool->shutdown)
        return -1;/*防止两次调用*/
    pool->shutdown = 1;

    /*唤醒所有等待线程，线程池要销毁了*/
    pthread_cond_broadcast (&(pool->queue_ready));

    /*阻塞等待线程退出，否则就成僵尸了*/
    int i;
    for (i = 0; i < pool->max_thread_num; i++)
        pthread_join (pool->threadid[i], NULL);
    free (pool->threadid);

    /*销毁等待队列*/
    CThread_worker *head = NULL;
    while (pool->queue_head != NULL)
    {
        head = pool->queue_head;
        pool->queue_head = pool->queue_head->next;
        free (head);
    }
    /*条件变量和互斥量也别忘了销毁*/
    pthread_mutex_destroy(&(pool->queue_lock));//销毁互斥锁
    pthread_cond_destroy(&(pool->queue_ready));//销毁条件变量状态

    free (pool);
    /*销毁后指针置空是个好习惯*/
    pool=NULL;
    return 0;
}



void * thread_routine (void *arg)
{

    //printf ("starting thread 0x%x\n", pthread_self ());

    while (1)
    {
        pthread_mutex_lock (&(pool->queue_lock));//阻塞申请互斥锁
        /*如果等待队列为0并且不销毁线程池，则处于阻塞状态; 注意
        pthread_cond_wait是一个原子操作，等待前会解锁，唤醒后会加锁*/
        while (pool->cur_queue_size == 0 && !pool->shutdown)
        {
         //  printf ("thread 0x%x is waiting\n", pthread_self ());
            pthread_cond_wait (&(pool->queue_ready), &(pool->queue_lock));
        }

        /*线程池要销毁了*/
        if (pool->shutdown)
        {
            /*遇到break,continue,return等跳转语句，千万不要忘记先解锁*/
            pthread_mutex_unlock (&(pool->queue_lock));//释放互斥锁
           // printf ("thread 0x%x will exit\n", pthread_self ());
            pthread_exit (NULL);
        }

      //  printf ("thread 0x%x is starting to work\n", pthread_self ());

        /*assert是调试的好帮手*/
        assert (pool->cur_queue_size != 0);
        assert (pool->queue_head != NULL);

        /*等待队列长度减去1，并取出链表中的头元素*/
        pool->cur_queue_size--;
        CThread_worker *worker = pool->queue_head;
        pool->queue_head = worker->next;
        pthread_mutex_unlock (&(pool->queue_lock));//释放互斥锁

        /*调用回调函数，执行任务*/
        (*(worker->process)) (worker->arg);
        free (worker);
        worker = NULL;
    }
    /*这一句应该是不可达的*/
    pthread_exit (NULL);
}

void Pthread_Analy()
{
	int *workingnum = (int *) malloc (sizeof (int) * 5);//动态分布6个存储空间,用于存放任务名
    int i=0;
    for (i = 0; i < 4; i++)//动态分配任务至线程池
		{
			workingnum[i] = i;
		if (i==0)
		{
		 pool_add_worker (myprocess0, &workingnum[i]);
		}
		if (i==1)
			{
		    pool_add_worker (myprocess1, &workingnum[i]);
			}
		if (i==2)
			{
			pool_add_worker (myprocess2, &workingnum[i]);
			}
			//if (i==0)
				//{
				//pool_add_worker (myprocess3, &workingnum[i]);
				//}
			//if (i==4)
				//{
				//pool_add_worker (myprocess4, &workingnum[i]);
				//}
		if (i==3)
			{
			//pool_add_worker (myprocess5, &workingnum[i]);
		}
		}
		free (workingnum);//释放任务名的存储空间
        	/*等待所有任务完成*/
        	//sleep (5);
        	/*销毁线程池*/
        	//pool_destroy ();
}





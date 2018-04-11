/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include "swoole.h"

static int swMutex_lock(swLock *lock);
static int swMutex_unlock(swLock *lock);
static int swMutex_trylock(swLock *lock);
static int swMutex_free(swLock *lock);

int swMutex_create(swLock *lock, int use_in_process)
{
    int ret;
    bzero(lock, sizeof(swLock));
    lock->type = SW_MUTEX;
    // #include <pthread.h>
    // int pthread_mutexattr_init(pthread_mutexattr_t *attr);
    // 若成功，返回0；否则，返回错误编号。
    // pthread_mutexattr_init函数将用默认的互斥量属性初始化pthread_mutexattr_t结构。
    // 值得注意的3个属性是：进程共享属性、健壮属性及类型属性。    ß
    pthread_mutexattr_init(&lock->object.mutex.attr);
    if (use_in_process == 1)
    {
        // #include <pthread.h>
        // int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);
        // 若成功，返回0；否则，返回错误编号。
        // 使用pthread_mutexattr_setpshared函数修改进程共享属性。
        // 存在这样的机制：允许相互独立的多个进程把同一个内存数据块映射到它们各自独立的地址空间中。
        // 就像多个线程访问共享数据一样，多个进程访问共享数据通常也需要同步。
        // 如果进程共享互斥量属性设置为PTHREAD_PROCESS_SHARED,从多个进程彼此之间共享的内存数据块中分配的互斥量就可以用于同步这些进程。
        pthread_mutexattr_setpshared(&lock->object.mutex.attr, PTHREAD_PROCESS_SHARED);
    }

    // #include <pthread.h>
    // int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr);
    // 若成功，返回0；否则，返回错误编号
    // 在使用互斥变量以前，必须首先对它进行初始化，可以把它设置为常量PTHREAD_MUTEX_INITIALIZER（只适用于静态分配的互斥量）
    // 也可以通过调用pthread_mutex_init函数进行初始化。
    // 如果动态分配互斥量（例如，通过malloc函数），在释放内存前需要调用pthread_mutex_destroy。
    // 要用默认的属性初始化互斥量，只需把attr设为NULL。
    if ((ret = pthread_mutex_init(&lock->object.mutex._lock, &lock->object.mutex.attr)) < 0)
    {
        return SW_ERR;
    }
    lock->lock = swMutex_lock;
    lock->unlock = swMutex_unlock;
    lock->trylock = swMutex_trylock;
    lock->free = swMutex_free;
    return SW_OK;
}

// 上锁
static int swMutex_lock(swLock *lock)
{
    // #include <pthread.h>
    // int pthread_mutex_lock(pthread_mutex_t *mute);
    // 若成功，返回0；否则，返回错误编号
    // 对互斥量进行加锁，需要调用pthread_mutex_lock。
    // 如果互斥量已经上锁，调用线程将阻塞直到互斥量被解锁。
    return pthread_mutex_lock(&lock->object.mutex._lock);
}

// 解锁
static int swMutex_unlock(swLock *lock)
{
    // #include <pthread.h>
    // int pthread_mutex_unlock(pthread_mutex_t *mute);
    // 若成功，返回0；否则，返回错误编号
    // 对互斥量解锁，需要调用pthread_mutex_unlock。
    return pthread_mutex_unlock(&lock->object.mutex._lock);
}

// 非阻塞上锁，不管是否成功都立即返回
static int swMutex_trylock(swLock *lock)
{
    // #include <pthread.h>
    // int pthread_mutex_trylock(pthread_mutex_t *mute);
    // 若成功，返回0；否则，返回错误编号
    // 如果线程不希望被阻塞，它可以使用pthread_mutex_trylock尝试对互斥量进行加锁。
    // 如果调用pthread_mutex_trylock时互斥量处于未锁住状态，那么pthread_mutex_trylock将锁住互斥量，
    // 不会出现阻塞直至返回0，否则pthread_mutex_trylock就会失败，不能锁住互斥量，返回EBUSY。
    return pthread_mutex_trylock(&lock->object.mutex._lock);
}

#ifdef HAVE_MUTEX_TIMEDLOCK
int swMutex_lockwait(swLock *lock, int timeout_msec)
{
    struct timespec timeo;
    timeo.tv_sec = timeout_msec / 1000;
    timeo.tv_nsec = (timeout_msec - timeo.tv_sec * 1000) * 1000 * 1000;
    // #include <pthread.h>
    // #include <time.h>
    // int pthread_mutex_timedlock(pthread_mutex_t *restrict mutex, const struct timespec *restrict tsptr);
    // 若成功，返回0；否则，返回错误编号。
    // 当线程试图获取一个已加锁的互斥量时，pthread_mutex_timedlock互斥量原语允许绑定线程阻塞时间。
    // pthread_mutex_timedlock函数与pthread_mutex_lock是基本等价的，
    // 但是在达到超时时间值时，pthread_mutex_timedlock不会对互斥量加锁，而是返回错误码ETIEMDOUT。
    return pthread_mutex_timedlock(&lock->object.mutex._lock, &timeo);
}
#else
// 上锁等待，超时时间内一直尝试上锁
int swMutex_lockwait(swLock *lock, int timeout_msec)
{
    int sub = 1;
    int sleep_ms = 1000;

    if (timeout_msec > 100)
    {
        sub = 10;
        sleep_ms = 10000;
    }

    while( timeout_msec > 0)
    {
        if (pthread_mutex_trylock(&lock->object.mutex._lock) == 0)
        {
            return 0;
        }
        else
        {
            usleep(sleep_ms);
            timeout_msec -= sub;
        }
    }
    return ETIMEDOUT;
}
#endif

// 释放互斥锁
static int swMutex_free(swLock *lock)
{
    // #include <pthread.h>
    // int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
    // 若成功，返回0；否则，返回错误编号。
    // 用pthread_mutexattr_destroy来反初始化。
    pthread_mutexattr_destroy(&lock->object.mutex.attr);
    // #include <pthread.h>
    // int pthread_mutex_destroy(pthread_mutex_t *restrict mutex);
    // 若成功，返回0；否则，返回错误编号
    // 如果动态分配互斥量（例如，通过malloc函数），在释放内存前需要调用pthread_mutex_destroy。
    return pthread_mutex_destroy(&lock->object.mutex._lock);
}

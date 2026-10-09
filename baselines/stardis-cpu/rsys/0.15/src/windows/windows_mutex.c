#include "windows_platform.h"
#include <process.h> /* For _beginthreadex/_endthreadex */
#include "../mem_allocator.h"
#include "../mutex.h"

struct mutex*
mutex_create(void)
{
    struct mutex* mutex = mem_alloc(sizeof(struct mutex));
    if (mutex) {
        InitializeCriticalSection(&mutex->cs);
    }
    return mutex;
}

void
mutex_destroy(struct mutex* mutex)
{
    ASSERT(mutex);
    DeleteCriticalSection(&mutex->cs);
    mem_rm(mutex);
}

void
mutex_lock(struct mutex* mutex)
{
    ASSERT(mutex);
    EnterCriticalSection(&mutex->cs);
}

void
mutex_unlock(struct mutex* mutex)
{
    ASSERT(mutex);
    LeaveCriticalSection(&mutex->cs);
}

/*******************************************************************************
 * Read Write mutex - Windows没有原生读写锁，使用SRWLOCK（Vista+）
 ******************************************************************************/
struct mutex_rw*
mutex_rw_create(void)
{
    struct mutex_rw* mutex = mem_alloc(sizeof(struct mutex_rw));
    if (mutex) {
        InitializeSRWLock(&mutex->srwlock);
    }
    return mutex;
}

void
mutex_rw_destroy(struct mutex_rw* mutex)
{
    ASSERT(mutex);
    /* SRWLOCK不需要销毁，只需释放内存 */
    mem_rm(mutex);
}

void
mutex_rw_rlock(struct mutex_rw* mutex)
{
    ASSERT(mutex);
    AcquireSRWLockShared(&mutex->srwlock);
}

void
mutex_rw_wlock(struct mutex_rw* mutex)
{
    ASSERT(mutex);
    AcquireSRWLockExclusive(&mutex->srwlock);
}

void
mutex_rw_unlock(struct mutex_rw* mutex)
{
    ASSERT(mutex);
    ReleaseSRWLockExclusive(&mutex->srwlock); /* 释放独占锁 */
    /* 注意：Windows的ReleaseSRWLockExclusive/Shared函数是相同的 */
}
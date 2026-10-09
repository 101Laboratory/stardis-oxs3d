#include "windows_platform.h"
#include "../condition.h"
#include "../mem_allocator.h"

struct cond*
cond_create(void)
{
    struct cond* cond = mem_alloc(sizeof(struct cond));
    if (cond) {
        InitializeConditionVariable(&cond->cv);
    }
    return cond;
}

void
cond_destroy(struct cond* cond)
{
    ASSERT(cond);
    /* Windows CONDITION_VARIABLE不需要显式销毁 */
    mem_rm(cond);
}

void
cond_wait(struct cond* cond, struct mutex* mutex)
{
    ASSERT(cond && mutex);
    /* 现在通过windows_platform.h可以访问mutex的内部结构 */
    SleepConditionVariableCS(&cond->cv, 
                            &mutex->cs, 
                            INFINITE);
}

void
cond_signal(struct cond* cond)
{
    ASSERT(cond);
    WakeConditionVariable(&cond->cv);
}

void
cond_broadcast(struct cond* cond)
{
    ASSERT(cond);
    WakeAllConditionVariable(&cond->cv);
}
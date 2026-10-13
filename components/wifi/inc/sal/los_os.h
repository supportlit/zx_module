/* ----------------------------------------------------------------------------
 * Copyright (c) Huawei Technologies Co., Ltd. 2013-2020. All rights reserved.
 * Description: Semaphore HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */

/**
 * @defgroup los_os os
 * @ingroup kernel
 */

#ifndef _LOS_OS_H
#define _LOS_OS_H
#include "los_typedef.h"
#include <stdlib.h>
#include <string.h>
#include "osi_api.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

extern UINT8 *m_aucSysMem0;

/**
 * @ingroup los_sem
 * Max count of counting semaphores
 */
#define LOS_SEM_COUNT_MAX  0xFFFE

#define LOS_WAIT_FOREVER   OSI_WAIT_FOREVER

/**
 * @ingroup los_sem
 * Semaphore error code: The memory is insufficient.
 *
 * Value: 0x02000700.
 *
 * Solution: Allocate more memory.
 */
#define LOS_ERRNO_SEM_NO_MEMORY  LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x00)

/**
 * @ingroup los_sem
 * Semaphore error code: Invalid parameter.
 *
 * Value: 0x02000701.
 *
 * Solution: Change the passed-in invalid parameter value to a valid value.
 */
#define LOS_ERRNO_SEM_INVALID      LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x01)

/**
 * @ingroup los_sem
 * Semaphore error code: Null pointer.
 *
 * Value: 0x02000702.
 *
 * Solution: Change the passed-in null pointer to a valid non-null pointer.
 */
#define LOS_ERRNO_SEM_PTR_NULL     LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x02)

/**
 * @ingroup los_sem
 * Semaphore error code: No semaphore control structure is available.
 *
 * Value: 0x02000703.
 *
 * Solution: Perform corresponding operations based on the requirements in the code context.
 */
#define LOS_ERRNO_SEM_ALL_BUSY     LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x03)

/**
 * @ingroup los_sem
 * Semaphore error code: Invalid parameter that specifies the timeout interval.
 *
 * Value: 0x02000704.
 *
 * Solution: Change the passed-in parameter value to a valid nonzero value.
 */
#define LOS_ERRNO_SEM_UNAVAILABLE  LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x04)

/**
 * @ingroup los_sem
 * Semaphore error code: The API is called during an interrupt, which is forbidden.
 *
 * Value: 0x02000705.
 *
 * Solution: Do not call the API during an interrupt.
 */
#define LOS_ERRNO_SEM_PEND_INTERR  LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x05)

/**
 * @ingroup los_sem
 * Semaphore error code: The task is unable to request a semaphore because task scheduling is locked.
 *
 * Value: 0x02000706.
 *
 * Solution: Do not call LOS_SemPend when task scheduling is locked.
 */
#define LOS_ERRNO_SEM_PEND_IN_LOCK LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x06)

/**
 * @ingroup los_sem
 * Semaphore error code: The request for a semaphore times out.
 *
 * Value: 0x02000707.
 *
 * Solution: Change the passed-in parameter value to the value within the valid range.
 */
#define LOS_ERRNO_SEM_TIMEOUT      LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x07)

/**
 * @ingroup los_sem
 * Semaphore error code: The times of semaphore release exceed the maximum times permitted.
 *
 * Value: 0x02000708.
 *
 * Solution: Perform corresponding operations based on the requirements in the code context.
 */
#define LOS_ERRNO_SEM_OVERFLOW     LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x08)

/**
 * @ingroup los_sem
 * Semaphore error code: The queue of the tasks that are waiting on the semaphore control
 *                       structure is not null.
 *
 * Value: 0x02000709.
 *
 * Solution: Delete the semaphore after awaking all tasks that are waiting on the semaphore.
 */
#define LOS_ERRNO_SEM_PENDED       LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x09)

/**
 * @ingroup los_sem
 * Semaphore error code: The API is called in system-level callback, which is forbidden.
 *            old usage: The API is called in software timer callback, which is forbidden (LOS_ERRNO_SEM_PEND_SWTERR).
 *
 * Value: 0x0200070A.
 *
 * Solution: Do not call the API in the system-level callback.
 * @deprecated This error code is obsolete since LiteOS 5.0.0.
 */
#define LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK       LOS_ERRNO_OS_ERROR(LOS_MOD_SEM, 0x0A)

/**
 * @ingroup los_sem
 * @brief Create a semaphore.
 *
 * @par Description:
 * This API is used to create a semaphore control structure according to the initial number of available semaphores
 * specified by count and return the ID of this semaphore control structure.
 * @attention
 * None.
 *
 * @param count       [IN] Initial number of available semaphores. The value range is [0, LOS_SEM_COUNT_MAX).
 * @param semHandle   [OUT] ID of the semaphore control structure that is initialized.
 *
 * @retval #LOS_ERRNO_SEM_PTR_NULL  The passed-in semHandle value is NULL.
 * @retval #LOS_ERRNO_SEM_OVERFLOW  The passed-in count value is greater than the maximum number of available
 * semaphores.
 * @retval #LOS_ERRNO_SEM_ALL_BUSY  No semaphore control structure is available.
 * @retval #LOS_OK   The semaphore is successfully created.
 * @par Dependency:
 * <ul><li>los_sem.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_SemDelete | LOS_BinarySemCreate
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_SemCreate(UINT16 count, uint32_t *semHandle);

/**
 * @ingroup los_sem
 * @brief Create a binary semaphore.
 *
 * @par Description:
 * This API is used to create a binary semaphore control structure according to the initial number of
 * available semaphores specified by count and return the ID of this semaphore control structure.
 * @attention
 * None.
 *
 * @param count        [IN] Initial number of available semaphores. The value range is [0, 1].
 * @param semHandle    [OUT] ID of the semaphore control structure that is initialized.
 *
 * @retval #LOS_ERRNO_SEM_PTR_NULL     The passed-in semHandle value is NULL.
 * @retval #LOS_ERRNO_SEM_OVERFLOW     The passed-in count value is greater than the maximum number of
 *                                     available semaphores.
 * @retval #LOS_ERRNO_SEM_ALL_BUSY     No semaphore control structure is available.
 * @retval #LOS_OK   The semaphore is successfully created.
 * @par Dependency:
 * <ul><li>los_sem.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_SemCreate | LOS_SemDelete
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_BinarySemCreate(UINT16 count, uint32_t *semHandle);

/**
 * @ingroup los_sem
 * @brief Delete a semaphore.
 *
 * @par Description:
 * This API is used to delete a semaphore control structure that has an ID specified by semHandle.
 * @attention
 * The specified sem id must be created first before deleting it.
 *
 * @param semHandle   [IN] ID of the semaphore control structure to be deleted. The ID of the semaphore
 *                           control structure is obtained from semaphore creation.
 *
 * @retval #LOS_ERRNO_SEM_INVALID  The passed-in semHandle value is invalid.
 * @retval #LOS_ERRNO_SEM_PENDED   The queue of the tasks that are waiting on the semaphore control structure is
 * not null.
 * @retval #LOS_OK   The semaphore control structure is successfully deleted.
 * @par Dependency:
 * <ul><li>los_sem.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_SemCreate | LOS_BinarySemCreate
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_SemDelete(uint32_t semHandle);

/**
 * @ingroup los_sem
 * @brief Request a semaphore.
 *
 * @par Description:
 * This API is used to request a semaphore based on the semaphore control structure ID specified by semHandle and the
 * parameter that specifies the timeout period.
 * @attention
 * <ul>
 * <li>Do not pend sem during an interrupt.</li>
 * <li>Do not pend sem in a system task, such as idle or swtmr task.</li>
 * <li>The specified sem id must be created first. </li>
 * <li>Do not recommend to use this API in software timer callback. </li>
 * </ul>
 *
 * @param semHandle [IN] ID of the semaphore control structure to be requested. The ID of the semaphore control
 *                       structure is obtained from semaphore creation.
 * @param timeout   [IN] Timeout interval for waiting on the semaphore. The value range is [0, 0xFFFFFFFF].
 *                       If the value is set to 0, the semaphore is not waited on. If the value is set to 0xFFFFFFFF,
 *                       the semaphore is waited on forever(unit: Tick).
 *
 * @retval #LOS_ERRNO_SEM_INVALID             The passed-in semHandle value is invalid.
 * @retval #LOS_ERRNO_SEM_UNAVAILABLE         There is no available semaphore resource.
 * @retval #LOS_ERRNO_SEM_PEND_INTERR         The API is called during an interrupt, which is forbidden.
 * @retval #LOS_ERRNO_SEM_PEND_IN_LOCK        The task is unable to request a semaphore because task scheduling
 *                                            is locked.
 * @retval #LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK The API is called in a system task, such as idle task or software
 *                                            timer, which is forbidden.
 * @retval #LOS_ERRNO_SEM_TIMEOUT             The request for the semaphore times out.
 * @retval #LOS_OK   The semaphore request succeeds.
 * @par Dependency:
 * <ul><li>los_sem.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_SemPost | LOS_SemCreate | LOS_BinarySemCreate
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_SemPend(uint32_t semHandle, uint32_t timeout);

/**
 * @ingroup los_sem
 * @brief Release a semaphore.
 *
 * @par Description:
 * This API is used to release a semaphore that has a semaphore control structure ID specified by semHandle.
 * @attention
 * The specified sem id must be created first.
 *
 * @param semHandle   [IN] ID of the semaphore control structure to be released.The ID of the semaphore control
 *                         structure is obtained from semaphore creation.
 *
 * @retval #LOS_ERRNO_SEM_INVALID      The passed-in semHandle value is invalid.
 * @retval #LOS_ERRNO_SEM_OVERFLOW     The times of semaphore release exceed the maximum times permitted.
 * @retval #LOS_OK                     The semaphore is successfully released.
 * @par Dependency:
 * <ul><li>los_sem.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_SemPend | LOS_SemCreate | LOS_BinarySemCreate
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_SemPost(uint32_t semHandle);

uint32_t LOS_MuxCreate(uint32_t *muxHandle);

/**
 * @ingroup los_mux
 * @brief Delete a mutex.
 *
 * @par Description:
 * This API is used to delete a specified mutex. Return LOS_OK if the mutex deletion is successfully,
 * otherwise return specific error code.
 * @attention
 * <ul>
 * <li>The specific mutex should be created firstly.</li>
 * <li>The mutex can be deleted successfully only when there is no other tasks pend on it.</li>
 * </ul>
 *
 * @param muxHandle   [IN] The mutex handle to be deleted. The value of handle should be in
 *                         [0, LOSCFG_BASE_IPC_MUX_LIMIT - 1].
 *
 * @retval #LOS_ERRNO_MUX_INVALID            Invalid handle or mutex in use.
 * @retval #LOS_ERRNO_MUX_PENDED             Tasks pended on this mutex.
 * @retval #LOS_OK                           The mutex is successfully deleted.
 * @par Dependency:
 * <ul><li>los_mux.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_MuxCreate
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_MuxDelete(uint32_t muxHandle);

/**
 * @ingroup los_mux
 * @brief Wait to lock a mutex.
 *
 * @par Description:
 * This API is used to wait for a specified period of time to lock a mutex.
 * @attention
 * <ul>
 * <li>The specific mutex should be created firstly.</li>
 * <li>The function fails if the mutex that is waited on is already locked by another thread when the task scheduling
 * is disabled.</li>
 * <li>Do not wait on a mutex during an interrupt.</li>
 * <li>The priority inheritance protocol is supported. If a higher-priority thread is waiting on a mutex, it changes
 * the priority of the thread that owns the mutex to avoid priority inversion.</li>
 * <li>A recursive mutex can be locked more than once by the same thread.</li>
 * <li>Do not recommend to use this API in software timer callback. </li>
 * </ul>
 *
 * @param muxHandle    [IN] The mutex handle to be waited for. The value of handle should be
 *                          in [0, LOSCFG_BASE_IPC_MUX_LIMIT - 1].
 * @param timeout      [IN] Waiting time. The value range is [0, LOS_WAIT_FOREVER](unit: Tick).
 *
 * @retval #LOS_ERRNO_MUX_INVALID            The mutex state (for example, the mutex does not exist or is not in use)
 *                                           is not applicable for the current operation.
 * @retval #LOS_ERRNO_MUX_UNAVAILABLE        The mutex fails to be locked because it is locked by another thread and
 * a period of time is not set for waiting for the mutex to become available.
 * @retval #LOS_ERRNO_MUX_PEND_INTERR        The mutex is being locked during an interrupt.
 * @retval #LOS_ERRNO_MUX_PEND_IN_LOCK       The mutex is waited on when the task scheduling is disabled.
 * @retval #LOS_ERRNO_MUX_TIMEOUT            The mutex waiting times out.
 * @retval #LOS_OK                           The mutex is successfully locked.
 * @par Dependency:
 * <ul><li>los_mux.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_MuxCreate | LOS_MuxPost
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_MuxPend(uint32_t muxHandle, uint32_t timeout);

/**
 * @ingroup los_mux
 * @brief Release a mutex.
 *
 * @par Description:
 * This API is used to release a specified mutex.
 * @attention
 * <ul>
 * <li>The specific mutex should be created firstly.</li>
 * <li>Do not release a mutex during an interrupt.</li>
 * <li>If a recursive mutex is locked for many times, it must be unlocked for the same times to be released.</li>
 * </ul>
 *
 * @param muxHandle    [IN] The mutex handle to be released. The value of handle should be in
 *                          [0, LOSCFG_BASE_IPC_MUX_LIMIT - 1].
 *
 * @retval #LOS_ERRNO_MUX_INVALID            The mutex state (for example, the mutex does not exist or is not in use
 * or owned by other thread) is not applicable for the current operation.
 * @retval #LOS_ERRNO_MUX_PEND_INTERR        The mutex is being released during an interrupt.
 * @retval #LOS_OK                           The mutex is successfully released.
 * @par Dependency:
 * <ul><li>los_mux.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_MuxCreate | LOS_MuxPend
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_MuxPost(uint32_t muxHandle);

/**
 * @ingroup los_queue
 * @brief Create a message queue.
 *
 * @par Description:
 * This API is used to create a message queue. Different from LOS_QueueCreateStatic,
 * the user does not need to transfer the queue memory. In the API, system allocated
 * the queue memory automatically.
 * @attention
 * There are LOSCFG_BASE_IPC_QUEUE_LIMIT queues available, change it's value when necessary.
 * @param queueName        [IN]  Message queue name. Reserved parameter, not used for now.
 * @param len              [IN]  Queue length. The value range is [1,0xffff].
 * @param queueId          [OUT] ID of the queue control structure that is successfully created.
 * @param flags            [IN]  Queue mode. Reserved parameter, not used for now.
 * @param maxMsgSize       [IN]  Node size. The value range is [1,0xffff-4].
 *
 * @retval   #LOS_OK                            The message queue is successfully created.
 * @retval   #LOS_ERRNO_QUEUE_CB_UNAVAILABLE    The upper limit of the number of created queues is exceeded.
 * @retval   #LOS_ERRNO_QUEUE_CREATE_NO_MEMORY  Insufficient memory for queue creation.
 * @retval   #LOS_ERRNO_QUEUE_CREAT_PTR_NULL    Null pointer, queueId is NULL.
 * @retval   #LOS_ERRNO_QUEUE_PARA_ISZERO       The queue length or message node size passed in during queue
 * creation is 0.
 * @retval   #LOS_ERRNO_QUEUE_SIZE_TOO_BIG      The parameter usMaxMsgSize is larger than 0xffff - 4.
 * @par Dependency:
 * <ul><li>los_queue.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_QueueDelete | LOS_QueueCreateStatic
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_QueueCreate(const CHAR *queueName,
                                UINT16 len,
                                uint32_t *queueId,
                                uint32_t flags,
                                UINT16 maxMsgSize);

/**
 * @ingroup los_queue
 * @brief Read a queue.
 *
 * @par Description:
 * This API is used to read data in a specified queue, and store the obtained data to the address specified
 * by bufferAddr. The address and the size of the data to be read are defined by users.
 * @attention
 * <ul>
 * <li>The specific queue should be created firstly.</li>
 * <li>Queue reading adopts the fist in first out (FIFO) mode. The data that is first stored in the queue is read
 * first.</li>
 * <li>bufferAddr stores the obtained data.</li>
 * <li>Do not read or write a queue in unblocking modes such as an interrupt.</li>
 * <li>This API cannot be called before the Huawei LiteOS is initialized.</li>
 * <li>The argument timeout is a relative time.</li>
 * <li>Do not call this API in software timer callback. </li>
 * </ul>
 *
 * @param queueId        [IN]     Queue ID created by LOS_QueueCreate or LOS_QueueCreateStatic.
 *                                The value range is [1,LOSCFG_BASE_IPC_QUEUE_LIMIT].
 * @param bufferAddr     [OUT]    Starting address that stores the obtained data. The starting address must not be
 * null.
 * @param bufferSize     [IN/OUT] Where to maintain the buffer wanted-size before read, and the real-size after read.
 * @param timeout        [IN]     Expiry time. The value range is [0,LOS_WAIT_FOREVER](unit: Tick).
 *
 * @retval   #LOS_OK                              The queue is successfully read.
 * @retval   #LOS_ERRNO_QUEUE_INVALID             The handle of the queue that is being read is invalid.
 * @retval   #LOS_ERRNO_QUEUE_READ_PTR_NULL       The pointer passed in during queue reading is null.
 * @retval   #LOS_ERRNO_QUEUE_READSIZE_IS_INVALID The buffer size passed in during queue reading is invalid.
 * @retval   #LOS_ERRNO_QUEUE_READ_IN_INTERRUPT   The queue cannot be read during an interrupt when the time for
 * waiting to processing the queue expires.
 * @retval   #LOS_ERRNO_QUEUE_NOT_CREATE          The queue to be read is not created.
 * @retval   #LOS_ERRNO_QUEUE_ISEMPTY             No resource is in the queue that is being read when the time for
 * waiting to processing the queue expires.
 * @retval   #LOS_ERRNO_QUEUE_PEND_IN_LOCK        The task is forbidden to be blocked on a queue when the task is
 * locked.
 * @retval   #LOS_ERRNO_QUEUE_TIMEOUT             The time set for waiting to processing the queue expires.
 * @retval   #LOS_ERRNO_QUEUE_READ_SIZE_TOO_SMALL The buffer size passed in during queue reading is less than
 * the queue size.
 * @par Dependency:
 * <ul><li>los_queue.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_QueueWriteCopy | LOS_QueueWriteHeadCopy | LOS_QueueCreate | LOS_QueueCreateStatic
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_QueueReadCopy(uint32_t queueId,
                                  VOID *bufferAddr,
                                  uint32_t *bufferSize,
                                  uint32_t timeout);

/**
 * @ingroup los_queue
 * @brief Write data into a queue.
 *
 * @par Description:
 * This API is used to write the data of the size specified by bufferSize and stored at the address specified by
 * bufferAddr into a queue.
 * @attention
 * <ul>
 * <li>The specific queue should be created firstly.</li>
 * <li>Do not read or write a queue in unblocking modes such as interrupt.</li>
 * <li>This API cannot be called before the Huawei LiteOS is initialized.</li>
 * <li>The data to be written is of the size specified by bufferSize and is stored at the address specified by
 * bufferAddr.</li>
 * <li>The argument timeout is a relative time.</li>
 * <li>Do not call this API in software timer callback. </li>
 * </ul>
 *
 * @param queueId        [IN]  Queue ID created by LOS_QueueCreate or LOS_QueueCreateStatic.
 *                             The value range is [1,LOSCFG_BASE_IPC_QUEUE_LIMIT].
 * @param bufferAddr     [IN]  Starting address that stores the data to be written.The starting address must
 *                             not be null.
 * @param bufferSize     [IN]  Passed-in buffer size. The value range is [1,USHRT_MAX - sizeof(uint32_t)].
 * @param timeout        [IN]  Expiry time. The value range is [0,LOS_WAIT_FOREVER](unit: Tick).
 *
 * @retval   #LOS_OK                              The data is successfully written into the queue.
 * @retval   #LOS_ERRNO_QUEUE_INVALID             The queue handle passed in during queue writing is invalid.
 * @retval   #LOS_ERRNO_QUEUE_WRITE_PTR_NULL      The pointer passed in during queue writing is null.
 * @retval   #LOS_ERRNO_QUEUE_WRITESIZE_ISZERO    The buffer size passed in during queue writing is 0.
 * @retval   #LOS_ERRNO_QUEUE_WRITE_IN_INTERRUPT  The queue cannot be written during an interrupt when the time
 * for waiting to processing the queue expires.
 * @retval   #LOS_ERRNO_QUEUE_NOT_CREATE          The queue into which the data is written is not created.
 * @retval   #LOS_ERRNO_QUEUE_WRITE_SIZE_TOO_BIG  The buffer size passed in during queue writing is bigger than
 * the queue size.
 * @retval   #LOS_ERRNO_QUEUE_ISFULL              No free node is available during queue writing.
 * @retval   #LOS_ERRNO_QUEUE_PEND_IN_LOCK        The task is forbidden to be blocked on a queue when
 * the task is locked.
 * @retval   #LOS_ERRNO_QUEUE_TIMEOUT             The time set for waiting to processing the queue expires.
 * @par Dependency:
 * <ul><li>los_queue.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_QueueReadCopy | LOS_QueueWriteHeadCopy | LOS_QueueCreate | LOS_QueueCreateStatic
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_QueueWriteCopy(uint32_t queueId,
                                   VOID *bufferAddr,
                                   uint32_t bufferSize,
                                   uint32_t timeout);

/**
 * @ingroup los_queue
 * @brief Delete a queue.
 *
 * @par Description:
 * This API is used to delete a queue.
 * @attention
 * <ul>
 * <li>This API cannot be used to delete a queue that is not created.</li>
 * <li>A synchronous queue fails to be deleted if any tasks are blocked on it, or some queues are being read or
 * written.</li>
 * </ul>
 *
 * @param queueId     [IN] Queue ID created by LOS_QueueCreate or LOS_QueueCreateStatic.
 *                         The value range is [1,LOSCFG_BASE_IPC_QUEUE_LIMIT].
 *
 * @retval   #LOS_OK                      The queue is successfully deleted.
 * @retval   #LOS_NOK                     Failed to release the queue memory.
 * @retval   #LOS_ERRNO_QUEUE_NOT_FOUND   The queue cannot be found.
 * @retval   #LOS_ERRNO_QUEUE_NOT_CREATE  The queue handle passed in when the queue is being deleted is
 * incorrect.
 * @retval   #LOS_ERRNO_QUEUE_IN_TSKUSE   The queue that blocks a task cannot be deleted.
 * @retval   #LOS_ERRNO_QUEUE_IN_TSKWRITE Queue reading and writing are not synchronous.
 * @par Dependency:
 * <ul><li>los_queue.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_QueueCreate | LOS_QueueCreateStatic
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_QueueDelete(uint32_t queueId);

/**
 * @ingroup los_memory
 * @brief Allocate dynamic memory.
 *
 * @par Description:
 * This API is used to allocate a memory block of which the size is specified.
 * @attention
 * <ul>
 * <li>The input pool parameter must be initialized via func LOS_MemInit.</li>
 * <li>The size of the input parameter size can not be greater than the memory pool size that specified at the second
 * input parameter of LOS_MemInit.</li>
 * <li>The size of the input parameter size must be four byte-aligned.</li>
 * </ul>
 *
 * @param  pool    [IN] Pointer to the memory pool that contains the memory block to be allocated.
 * @param  size    [IN] Size of the memory block to be allocated (unit: byte).
 *
 * @retval #NULL          The memory fails to be allocated.
 * @retval #VOID*         The memory is successfully allocated, and the API returns the pointer to
 *                        the allocated memory block.
 * @par Dependency:
 * <ul><li>los_memory.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_MemRealloc | LOS_MemAllocAlign | LOS_MemFree
 * @since Huawei LiteOS V100R001C00
 */
extern VOID *LOS_MemAlloc(VOID *pool, uint32_t size);

/**
 * @ingroup los_memory
 * @brief Free dynamic memory.
 *
 * @par Description:
 * This API is used to free specified dynamic memory that has been allocated.
 * @attention
 * <ul>
 * <li>The input pool parameter must be initialized via func LOS_MemInit.</li>
 * <li>The input ptr parameter must be allocated by LOS_MemAlloc or LOS_MemAllocAlign or LOS_MemRealloc.</li>
 * </ul>
 *
 * @param  pool  [IN] Pointer to the memory pool that contains the dynamic memory block to be freed.
 * @param  ptr   [IN] Starting address of the memory block to be freed.
 *
 * @retval #LOS_NOK      The memory block fails to be freed because the starting address of the memory block is
 *                       invalid, or the memory overwriting occurs.
 * @retval #LOS_OK       The memory block is successfully freed.
 * @par Dependency:
 * <ul><li>los_memory.h: the header file that contains the API declaration.</li></ul>
 * @see LOS_MemAlloc | LOS_MemRealloc | LOS_MemAllocAlign
 * @since Huawei LiteOS V100R001C00
 */
extern uint32_t LOS_MemFree(VOID *pool, VOID *ptr);

#ifdef __cplusplus
#if __cplusplus
}

#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_SEM_H */

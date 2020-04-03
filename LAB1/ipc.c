/**
 * @file     ipc.c
 * @Author   Dima Glushkov
 * @date     April, 2020
 * @brief    A simple IPC library for programming assignments
 *
 */

#include "common.h"
#include "ipc.h"
#include "pa1.h"


typedef struct {
    local_id cur_id;
    local_id children_num;
    int * read_fd, * write_fd;
} IoFdType;

/** Send a message to the process specified by id.
*
* @param self    Any data structure implemented by students to perform I/O
* @param dst     ID of recepient
* @param msg     Message to send
*
* @return 0 on success, any non-zero value on error
*/
int send(void * self, local_id dst, const Message * msg)
{
    write(((IoFdType*) self)->write_fd[dst], msg, sizeof(MessageHeader) + msg->s_header.s_payload_len);
    return 0;
}

//------------------------------------------------------------------------------

/** Send multicast message.
 *
 * Send msg to all other processes including parrent.
 * Should stop on the first error.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param msg     Message to multicast.
 *
 * @return 0 on success, any non-zero value on error
 */
int send_multicast(void * self, const Message * msg)
{
    IoFdType * io_fd = (IoFdType *) self;
    for (local_id i = 0; i <= io_fd->children_num; i++)
        if (i != io_fd -> cur_id)
            send(self, i, msg);

    return 0;
}

//------------------------------------------------------------------------------

/** Receive a message from the process specified by id.
 *
 * Might block depending on IPC settings.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param from    ID of the process to receive message from
 * @param msg     Message structure allocated by the caller
 *
 * @return 0 on success, any non-zero value on error
 */
int receive(void * self, local_id from, Message * msg);

//------------------------------------------------------------------------------

/** Receive a message from any process.
 *
 * Receive a message from any process, in case of blocking I/O should be used
 * with extra care to avoid deadlocks.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param msg     Message structure allocated by the caller
 *
 * @return 0 on success, any non-zero value on error
 */
int receive_any(void * self, Message * msg)
{
    IoFdType * io_fd = (IoFdType *) self;

    read(io_fd->read_fd[io_fd->cur_id], &(msg->s_header), sizeof(MessageHeader));
    read(io_fd->read_fd[io_fd->cur_id], &(msg->s_payload), msg->s_header.s_payload_len);

//    printf("DEBUG: %d -> %s\n", msg->s_header.s_payload_len, msg->s_payload);

    return 0;
}

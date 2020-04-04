#include "ipc.h"
#include "ipc_io.h"
#include <unistd.h>


int send(void * self, local_id dst, const Message * msg) {
    write(((IOLinker*) self)->write_fd[((IOLinker*) self)->cur_id][dst], msg, sizeof(MessageHeader) + msg->s_header.s_payload_len);
    return 0;
}


int send_multicast(void * self, const Message * msg) {
    IOLinker * io_fd = (IOLinker *) self;
    for (local_id dst = 0; dst <= io_fd->subprocs_num; dst++)
        if (dst != io_fd -> cur_id)
            send(self, dst, msg);
    return 0;
}


int receive(void * self, local_id from, Message * msg) {
    IOLinker * io_fd = (IOLinker *) self;

    read(io_fd->read_fd[from][io_fd->cur_id], &(msg->s_header), sizeof(MessageHeader));
    read(io_fd->read_fd[from][io_fd->cur_id], &(msg->s_payload), msg->s_header.s_payload_len);

    msg->s_payload[msg->s_header.s_payload_len] = 0;
    return 0;
}



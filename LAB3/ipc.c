#include "ipc.h"
#include "ipc_io.h"
#include <unistd.h>


int send(void * self, local_id dst, const Message * msg) {
    write(((IOLinker*) self)->write_fd[((IOLinker*) self)->balance.s_id][dst], msg, sizeof(MessageHeader) + msg->s_header.s_payload_len);
    return 0;
}


int send_multicast(void * self, const Message * msg) {
    IOLinker * io_fd = (IOLinker *) self;
    for (local_id dst = 0; dst <= io_fd->subprocs_num; dst++)
        if (dst != io_fd->balance.s_id)
            send(self, dst, msg);
    return 0;
}


int receive(void * self, local_id from, Message * msg) {
    IOLinker * io_fd = (IOLinker *) self;
    int num_bytes_read;

    do {
        num_bytes_read = read(io_fd->read_fd[from][io_fd->balance.s_id], &(msg->s_header), sizeof(MessageHeader));
    } while (num_bytes_read <= 0);

    read(io_fd->read_fd[from][io_fd->balance.s_id], &(msg->s_payload), msg->s_header.s_payload_len);
//    sleep(1);

    return 0;
}


int receive_any(void * self, Message * msg) {
    IOLinker * io_fd = (IOLinker *) self;
    int src_id = 0;

    while (1) {
        if (src_id > io_fd->subprocs_num)
            src_id = 0;

        if (src_id != io_fd->balance.s_id && read(io_fd->read_fd[src_id][io_fd->balance.s_id], &(msg->s_header), sizeof(MessageHeader)) > 0) {
            read(io_fd->read_fd[src_id][io_fd->balance.s_id], &(msg->s_payload), msg->s_header.s_payload_len);
            break;
        }

        src_id++;
    }

    return 0;
}


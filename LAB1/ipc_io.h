#ifndef __IFMO_DISTRIBUTED_CLASS_IPC_IO__H
#define __IFMO_DISTRIBUTED_CLASS_IPC_IO__H

typedef struct {
    local_id cur_id;
    local_id subprocs_num;
    int write_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1],
         read_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1];
} IOLinker;


#endif // __IFMO_DISTRIBUTED_CLASS_IPC_IO__H

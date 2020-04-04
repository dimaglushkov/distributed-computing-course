#ifndef __IFMO_DISTRIBUTED_CLASS_IO_FD__H
#define __IFMO_DISTRIBUTED_CLASS_IO_FD__H

#include <stdlib.h>

typedef struct {
    local_id cur_id;
    local_id subprocs_num;
    int ** read_fd, ** write_fd;
} IoFdType;


#endif // __IFMO_DISTRIBUTED_CLASS_IO_FD__H

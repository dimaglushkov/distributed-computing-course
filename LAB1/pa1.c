#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "ipc.h"
#include "ipc.c"

int get_amount_of_child(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-p")) {
        perror("Required argument -p not specified.\n");
        return -1;
    }
    return (int)strtol(argv[2], NULL, 10);
}


int wait_for_children(int child_num, pid_t * child_pids)
{

    for (int i = 0; i < child_num; i++)
        waitpid(child_pids[i], NULL, 0);
    return 0;
}

int notify_all(IoFdType * io_fd, Message * msg)
{
    send_multicast(io_fd, msg);

    for (local_id j = 1; j < io_fd->children_num; ++j) {
        Message * new_msg = (Message *) malloc(sizeof(Message));
        receive_any(io_fd, new_msg);
        printf("%d: %s", io_fd->cur_id, new_msg->s_payload);
        free(new_msg);
    }

    return 0;
}



int run_child(IoFdType io_fd)
{
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, io_fd.cur_id, getpid(), getppid());
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);
//    printf("DEBUG: %d\n", start_msg.s_header.s_payload_len);

    notify_all(&io_fd, &start_msg);

    Message done_msg;
    sprintf(done_msg.s_payload, log_done_fmt, io_fd.cur_id);
//    printf("DEBUG: %s", done_msg.s_payload);
    done_msg.s_header.s_magic = MESSAGE_MAGIC;
    done_msg.s_header.s_type = DONE;
    done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);
//    printf("DEBUG: %d\n", done_msg.s_header.s_payload_len);


    notify_all(&io_fd, &done_msg);

    return 0;
}

IoFdType create_pipes(int child_num)
{
    IoFdType io_fd;
    io_fd.children_num = child_num;
    io_fd.read_fd = (int *)malloc((child_num + 1) * sizeof(int));
    io_fd.write_fd = (int *)malloc((child_num + 1) * sizeof(int));

    for (int i = 0; i <= child_num; ++i) {
        int fds[2];
        pipe(fds);
        io_fd.read_fd[i] = fds[0];
        io_fd.write_fd[i] = fds[1];
    }

    return io_fd;
}

int fork_process(int child_num)
{
    pid_t *pids = (pid_t *)malloc((child_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    IoFdType io_fd = create_pipes(child_num);

    for (int i = 1; i <= child_num; i++)
    {
        pid_t fork_pid = fork();

        if (fork_pid == 0)
        {
            io_fd.cur_id = i;
            return run_child(io_fd);
        }

        if (fork_pid != 0)
            pids[i] = fork_pid;
    }

    wait_for_children(child_num, pids);
    free(pids);
    free(io_fd.write_fd);
    free(io_fd.read_fd);
    return 0;
}


int main(int argc, char **argv)
{
    int child_num = get_amount_of_child(argc, argv);

    return fork_process(child_num);
}


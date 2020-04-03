#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "logger.h"
#include "ipc.c"

int get_num_of_subprocs(int argc, char **argv) {
    local_id subprocs_num = -1;

    if (argc < 3)
        return -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i < argc - 1)
            subprocs_num = (local_id)strtol(argv[++i], NULL, 10);
    }
    return subprocs_num;
}


int confer_all(IoFdType * io_fd, Message * msg) {

    send_multicast(io_fd, msg);

    for (local_id j = 1; j < io_fd->subprocs_num; ++j) {
        Message * new_msg = (Message *) malloc(sizeof(Message));
        receive_any(io_fd, new_msg);
        free(new_msg);
    }

    return 0;
}



int run_subproc(IoFdType io_fd) {
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, io_fd.cur_id, getpid(), getppid());
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);

    log_write_all(start_msg.s_payload);

    confer_all(&io_fd, &start_msg);

    char * all_started = (char *) malloc(strlen(log_received_all_started_fmt));
    sprintf(all_started, log_received_all_started_fmt, io_fd.cur_id);
    log_write_all(all_started);
    free(all_started);

    Message done_msg;
    sprintf(done_msg.s_payload, log_done_fmt, io_fd.cur_id);
    done_msg.s_header.s_magic = MESSAGE_MAGIC;
    done_msg.s_header.s_type = DONE;
    done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

    log_write_all(done_msg.s_payload);

    confer_all(&io_fd, &done_msg);

    char * all_done = (char *) malloc(strlen(log_received_all_done_fmt));
    sprintf(all_done, log_received_all_done_fmt, io_fd.cur_id);
    log_write_all(all_done);
    free(all_done);

    return 0;
}

IoFdType create_pipes(local_id subproc_num) {
    IoFdType io_fd;
    io_fd.subprocs_num = subproc_num;
    io_fd.read_fd = (int *) malloc ((subproc_num + 1) * sizeof(int));
    io_fd.write_fd = (int *) malloc ((subproc_num + 1) * sizeof(int));

    for (int i = 0; i <= subproc_num; ++i) {
        int fds[2];
        pipe(fds);
        io_fd.read_fd[i] = fds[0];
        io_fd.write_fd[i] = fds[1];
    }

    return io_fd;
}


int run_proc(IoFdType io_fd, pid_t * subproc_pids){
    io_fd.cur_id = 0;
    for (int i = 0; i < io_fd.subprocs_num; i++) {
        Message msg;
        receive_any(&io_fd, &msg);
    }

    char * all_started = (char *) malloc(strlen(log_received_all_started_fmt));
    sprintf(all_started, log_received_all_started_fmt, io_fd.cur_id);
    log_write_all(all_started);
    free(all_started);

    for (int i = 0; i < io_fd.subprocs_num; i++) {
        Message msg;
        receive_any(&io_fd, &msg);
    }

    char * all_done = (char *) malloc(strlen(log_received_all_done_fmt));
    sprintf(all_done, log_received_all_done_fmt, io_fd.cur_id);
    log_write_all(all_done);
    free(all_done);

    for (int i = 0; i < io_fd.subprocs_num; i++)
        waitpid(subproc_pids[i], NULL, 0);
    return 0;
}


int main(int argc, char **argv) {
    local_id subprocs_num = get_num_of_subprocs(argc, argv);

    pid_t *pids = (pid_t *)malloc((subprocs_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    IoFdType io_fd = create_pipes(subprocs_num);

    log_begin();

    for (int i = 1; i <= subprocs_num; i++) {
        pid_t fork_pid = fork();

        if (fork_pid == 0) {
            io_fd.cur_id = i;
            return run_subproc(io_fd);
        }

        if (fork_pid != 0)
            pids[i] = fork_pid;
    }

    run_proc(io_fd, pids);

    log_finish();

    free(pids);
    free(io_fd.write_fd);
    free(io_fd.read_fd);

    return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "logger.h"
#include "io_fd.h"
#include "ipc.h"

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


void init_io_fd(IoFdType * io_fd) {
    io_fd->read_fd  = (int **)malloc((io_fd->subprocs_num + 1) * sizeof(int*));
    io_fd->write_fd = (int **)malloc((io_fd->subprocs_num + 1) * sizeof(int*));

    for(int i = 0; i <= io_fd->subprocs_num; i++) {
        io_fd->read_fd[i]  = (int *) malloc((io_fd->subprocs_num + 1) * sizeof(int));
        io_fd->write_fd[i] = (int *) malloc((io_fd->subprocs_num + 1) * sizeof(int));
    }
}


void free_io_fd(IoFdType * io_fd) {
    for(int i = 0; i <= io_fd->subprocs_num; i++) {
        free(io_fd->read_fd[i]);
        free(io_fd->write_fd[i]);
    }
    free(io_fd->read_fd);
    free(io_fd->write_fd);

}


int confer_all(IoFdType * io_fd, Message * msg) {

    send_multicast(io_fd, msg);

    for (local_id from = 1; from <= io_fd->subprocs_num; from++)
        if (from != io_fd->cur_id){
        Message * new_msg = (Message *) malloc(sizeof(Message));
        receive(io_fd, from, new_msg);
        free(new_msg);
        }

    return 0;
}


IoFdType create_pipes(local_id subproc_num) {
    IoFdType io_fd;
    io_fd.subprocs_num = subproc_num;
    init_io_fd(&io_fd);


    for (int src = 0; src <= subproc_num; src++)
        for (int dst = 0; dst <= subproc_num; dst++)
            if (src != dst){
                int fds[2];
                pipe(fds);
                io_fd.read_fd[src][dst] = fds[0];
                io_fd.write_fd[src][dst] = fds[1];
            }

    return io_fd;
}


int filter_pipes(IoFdType * io_fd){
    for(int src = 0; src <= io_fd->subprocs_num; src++)
        for(int dst = 0; dst <= io_fd->subprocs_num; dst++) {
            if (src != io_fd->cur_id) {
                close(io_fd->write_fd[src][dst]);
                io_fd->write_fd[src][dst] = 0;
            }
            if (dst != io_fd->cur_id) {
                close(io_fd->read_fd[src][dst]);
                io_fd->read_fd[src][dst] = 0;
            }
        }
    return 0;
}


int run_subproc(IoFdType io_fd) {

    filter_pipes(&io_fd);

//    printf("%d, %d\n", getpid(), io_fd.cur_id);

//    if (io_fd.cur_id == 1) {
//        printf("<<< %d >>>\n", io_fd.cur_id);
//        for (int i = 0; i <= io_fd.subprocs_num; i++) {
//            for (int j = 0; j <= io_fd.subprocs_num; j++)
//                printf("%2d/%2d ", io_fd.write_fd[i][j], io_fd.read_fd[i][j]);
//            printf("\n");
//        }
//    }

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

    free_io_fd(&io_fd);
    return 0;
}


int run_proc(IoFdType io_fd, pid_t * pids){
    io_fd.cur_id = 0;

    filter_pipes(&io_fd);

//    printf("<<< %d >>>\n", io_fd.cur_id);
//    for (int i = 0; i <= io_fd.subprocs_num; i++) {
//        for (int j = 0; j <= io_fd.subprocs_num; j++)
//            printf("%2d/%2d ", io_fd.write_fd[i][j], io_fd.read_fd[i][j]);
//        printf("\n");
//    }

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }

    char * all_started = (char *) malloc(strlen(log_received_all_started_fmt));
    sprintf(all_started, log_received_all_started_fmt, io_fd.cur_id);
    log_write_all(all_started);
    free(all_started);

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }

    char * all_done = (char *) malloc(strlen(log_received_all_done_fmt));
    sprintf(all_done, log_received_all_done_fmt, io_fd.cur_id);
    log_write_all(all_done);
    free(all_done);

    for (int i = 1; i <= io_fd.subprocs_num; i++)
        waitpid(pids[i], NULL, 0);
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
    free_io_fd(&io_fd);

    return 0;
}


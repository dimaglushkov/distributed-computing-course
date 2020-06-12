#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>


#include "logger.h"
#include "ipc_io.h"
#include "ipc.h"
#include "banking.h"


timestamp_t lamport_time = 0;


void set_time_msg(Message * msg){
    lamport_time++;
    msg->s_header.s_local_time = get_lamport_time();
}


int confer_all(IOLinker * io_fd, Message * msg) {
    set_time_msg(msg);
    send_multicast(io_fd, msg);

    for (local_id from = 1; from <= io_fd->subprocs_num; from++)
        if (from != io_fd->balance.s_id) {
            Message * new_msg = (Message *) malloc(sizeof(Message));
            receive(io_fd, from, new_msg);
            free(new_msg);
        }

    return 0;
}


timestamp_t get_lamport_time() {
    return lamport_time;
}


int request_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;

    io_fd->queue[io_fd->balance.s_id] = *io_fd->lamport_time_p;

    Message init_request_msg;
    init_request_msg.s_header.s_magic = MESSAGE_MAGIC;
    init_request_msg.s_header.s_type = CS_REQUEST;
    init_request_msg.s_payload[0] = '\0';
    init_request_msg.s_header.s_payload_len = 1;

    set_time_msg(&init_request_msg);
    send_multicast(io_fd, &init_request_msg);

    int waiting_in_queue = 1;
    int next_to_cs = 1;
    timestamp_t min_time = io_fd->queue[1];
    timestamp_t received_from[io_fd->subprocs_num + 1];
    for (int i = 0; i <= io_fd->subprocs_num; i++)
        received_from[i] = 0;

    do {
        Message *msg = (Message *) malloc(sizeof(Message));
        receive_any(&io_fd, msg);

        switch (msg->s_header.s_type) {
            case CS_REQUEST:
                // check if its a proper time
                received_from[io_fd->last_sender] = get_lamport_time();
                io_fd->queue[io_fd->last_sender] = get_lamport_time();

                msg->s_header.s_type = CS_REPLY;
                set_time_msg(msg);
                send(&io_fd, io_fd->last_sender, msg);
                break;

            case CS_REPLY:
                received_from[io_fd->last_sender] = get_lamport_time();
                break;

            case CS_RELEASE:
                received_from[io_fd->last_sender] = get_lamport_time();
                io_fd->queue[io_fd->last_sender] = 0;
                break;
        }
        free(msg);

        int received_from_everyone = 1;
        for (int i = 1; i <= io_fd->subprocs_num; i++){
            if (received_from[i] == 0)
                received_from_everyone = 0;
            if (io_fd->queue[i] < min_time){
                next_to_cs = i;
                min_time = io_fd->queue[i];
            }
        }

        if (received_from_everyone && next_to_cs == io_fd->balance.s_id)
            waiting_in_queue = 0;

    } while(waiting_in_queue);

    return 0;
}


int release_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;

    Message release_msg;
    release_msg.s_header.s_magic = MESSAGE_MAGIC;
    release_msg.s_header.s_type = CS_RELEASE;
    release_msg.s_payload[0] = '\0';
    release_msg.s_header.s_payload_len = 1;

    set_time_msg(&release_msg);
    send_multicast(io_fd, &release_msg);

    return 0;
}


IOLinker create_pipes(local_id subproc_num) {
    IOLinker io_fd;
    io_fd.subprocs_num = subproc_num;

    for (int src = 0; src <= subproc_num; src++)
        for (int dst = 0; dst <= subproc_num; dst++)
            if (src != dst){
                int fds[2];
                pipe(fds);

                // for non-blocking read
                fcntl(fds[0], F_SETFL, O_NONBLOCK);
                fcntl(fds[1], F_SETFL, O_NONBLOCK);

                io_fd.read_fd[src][dst] = fds[0];
                io_fd.write_fd[src][dst] = fds[1];
            }

    return io_fd;
}


int filter_pipes(IOLinker *io_fd){
    for(int src = 0; src <= io_fd->subprocs_num; src++)
        for(int dst = 0; dst <= io_fd->subprocs_num; dst++)
            if (src != dst) {
                if (src != io_fd->balance.s_id) {
                    close(io_fd->write_fd[src][dst]);
                    io_fd->write_fd[src][dst] = 0;
                }
                if (dst != io_fd->balance.s_id) {
                    close(io_fd->read_fd[src][dst]);
                    io_fd->read_fd[src][dst] = 0;
                }
            }
    return 0;
}


void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    IOLinker * io_fd = (IOLinker *) parent_data;

    Message start_of_transfer_msg;
    start_of_transfer_msg.s_header.s_type = TRANSFER;
    start_of_transfer_msg.s_header.s_local_time = get_lamport_time();
    start_of_transfer_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_of_transfer_msg.s_header.s_payload_len = sizeof(TransferOrder);

    TransferOrder order;
    order.s_src = src;
    order.s_dst = dst;
    order.s_amount = amount;

    memcpy(&start_of_transfer_msg.s_payload, &order, sizeof(TransferOrder));
    Message * end_of_transfer_msg = (Message *) malloc(sizeof(Message));

    set_time_msg(&start_of_transfer_msg);
    send(io_fd, src, &start_of_transfer_msg);
    receive(io_fd, dst, end_of_transfer_msg);
    free(end_of_transfer_msg);

//    end_of_transfer_msg->s_header.s_type == ACK

}


void print_history(const AllHistory * history)
{
    if (history == NULL) {
        fprintf(stderr, "print_history: history is NULL!\n");
        exit(1);
    }

    typedef struct {
        int balance;
        int pending;
    } Pair;

    int max_time = 0;
    int has_pending = 0;
    int nrows = history->s_history_len + 2; // 0 row (parent) is not used + Total row
    Pair table[nrows][MAX_T];
    memset(table, 0, sizeof(table));

    for (int i = 0; i < history->s_history_len; ++i) {
        for (int j = 0; j < history->s_history[i].s_history_len; ++j) {
            const BalanceState * change = &history->s_history[i].s_history[j];
            int id = history->s_history[i].s_id;
            table[id][change->s_time].balance = change->s_balance;
            table[id][change->s_time].pending = change->s_balance_pending_in;
            if (max_time < change->s_time) {
                max_time = change->s_time;
            }
            if (change->s_balance_pending_in > 0) {
                has_pending = 1;
            }
        }
    }

    if (max_time > MAX_T) {
        fprintf(stderr, "print_history: max value of s_time: %d, expected s_time < %d!\n",
                max_time, MAX_T);
        return;
    }

    // Calculate total sum
    for (int j = 0; j <= max_time; ++j) {
        int sum = 0;
        for (int i = 1; i <= history->s_history_len; ++i) {
            sum += table[i][j].balance + table[i][j].pending;
        }
        table[nrows-1][j].balance = sum;
        table[nrows-1][j].pending = 0;
    }

    // pretty print
    fflush(stderr);
    fflush(stdout);

    const char * cell_format_pending = " %d (%d) ";
    const char * cell_format = " %d ";

    char buf[128];
    int max_cell_width = 0;
    for (int i = 1; i <= history->s_history_len; ++i) {
        for (int j = 0; j <= max_time; ++j) {
            if (has_pending) {
                sprintf(buf, cell_format_pending, table[i][j].balance, table[i][j].pending);
            } else {
                sprintf(buf, cell_format, table[i][j].balance);
            }
            int width = strlen(buf);
            if (max_cell_width < width) {
                max_cell_width = width;
            }
        }
    }

    const char * const first_column_header = "Proc \\ time |";
    const int first_column_width = strlen(first_column_header);
    const int underscrores = (first_column_width + 1) + (max_cell_width + 1) * (max_time + 1);

    char hline[underscrores + 2];
    for (int i = 0; i < underscrores; ++i) {
        hline[i] = '-';
    }
    hline[underscrores] = '\n';
    hline[underscrores + 1] = '\0';

    if (has_pending) {
        printf("\nFull balance history for time range [0;%d], $balance ($pending):\n", max_time);
    } else {
        printf("\nFull balance history for time range [0;%d], $balance:\n", max_time);
    }
    printf(hline);

    printf("%s ", first_column_header);
    for (int j = 0; j <= max_time; ++j) {
        printf("%*d |", max_cell_width - 1, j);
    }
    printf("\n");
    printf(hline);

    for (int i = 1; i <= history->s_history_len; ++i) {
        printf("%11d | ", i);
        for (int j = 0; j <= max_time; ++j) {
            if (has_pending) {
                sprintf(buf, cell_format_pending, table[i][j].balance, table[i][j].pending);
            } else {
                sprintf(buf, cell_format, table[i][j].balance);
            }
            printf("%*s|", max_cell_width, buf);
        }
        printf("\n");
        printf(hline);
    }

    printf("      Total | ");
    for (int j = 0; j <= max_time; ++j) {
        printf("%*d |", max_cell_width - 1, table[nrows-1][j].balance);
    }
    printf("\n");
    printf(hline);
}


int run_subproc(IOLinker io_fd) {
    Message ack_msg;
    Message done_msg;
    Message balance_history_msg;
    TransferOrder * transfer;
    filter_pipes(&io_fd);

    int local_queue[io_fd.subprocs_num];

    // starting
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, get_lamport_time(), io_fd.balance.s_id, getpid(), getppid(), io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);

    log_write_all(start_msg.s_payload);

    confer_all(&io_fd, &start_msg);

    log_all_started(get_lamport_time(), io_fd.balance.s_id);

    // --------------------------------------
    // actual work

    char log_loop_operation[strlen(log_loop_operation_fmt)];

    if (!io_fd.use_critical_section) {
        // no --mutexl given
        for (int i = 1; i <= io_fd.balance.s_id * 5; i++) {
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
        }

        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

        log_write_all(done_msg.s_payload);

        confer_all(&io_fd, &done_msg);

        log_all_done(get_lamport_time(), io_fd.balance.s_id);

    } else {
        // --mutexl given
        int num_to_send = io_fd.balance.s_id * 5;
        for (int i = 1; i <= num_to_send; ++i) {
            request_cs(&io_fd);
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
            release_cs(&io_fd);
        }
        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

        log_write_all(done_msg.s_payload);

        confer_all(&io_fd, &done_msg);

        log_all_done(get_lamport_time(), io_fd.balance.s_id);

    }

    return 0;
}


int run_proc(IOLinker io_fd, pid_t * pids){
    io_fd.balance.s_id = 0;
    filter_pipes(&io_fd);

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_started(io_fd.balance.s_id);


    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_done(io_fd.cur_id);


    for (int i = 1; i <= io_fd.subprocs_num; i++)
        waitpid(pids[i], NULL, 0);
    return 0;
}


int main(int argc, char **argv) {
    local_id subprocs_num = -1;
    int * init_balances;
    int use_critical_section = 0;

    if (argc < 3)
        return -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i < argc - 1){
            subprocs_num = (local_id)strtol(argv[++i], NULL, 10);
            init_balances = (int*) malloc(sizeof(int) * subprocs_num);
            for (int j = 0; j < subprocs_num; j++)
                init_balances[j] = (int)strtol(argv[i + j + 1], NULL, 10);
        }
        if (!strcmp(argv[i], "--mutexl"))
            use_critical_section = 1;
    }

    pid_t *pids = (pid_t *)malloc((subprocs_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    IOLinker io_fd = create_pipes(subprocs_num);
    io_fd.lamport_time_p = &lamport_time;

    log_begin();

    for (int i = 1; i <= subprocs_num; i++) {
        pid_t fork_pid = fork();

        if (fork_pid == 0) {
            io_fd.use_critical_section = use_critical_section;
            io_fd.balance.s_id = i;
            io_fd.balance.s_history_len = 0;
            io_fd.queue = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = init_balances[i - 1];
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_lamport_time();
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
            io_fd.balance.s_history_len++;

            return run_subproc(io_fd);
        }

        if (fork_pid != 0)
            pids[i] = fork_pid;
    }

    run_proc(io_fd, pids);

    log_finish();
    free(init_balances);
    free(pids);

    return 0;
}

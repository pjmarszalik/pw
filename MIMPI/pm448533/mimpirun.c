/**
 * This file is for implementation of mimpirun program.
 * */

#include "mimpi_common.h"
#include "channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char * argv[]) {
    copies = atoi(argv[1]);
    char * path = argv[2];
    char * args[argc - 2];
    for(int i = 2; i <= argc; i++)
        args[i - 2] = argv[i];
    int fd[2];
    for(int i = 0; i < copies; i ++){
        for(int j = 0; j < copies; j ++){
            if(i != j){
                ASSERT_SYS_OK(channel(fd));
                ASSERT_SYS_OK(dup2(fd[0], recv_pipe_num(j, i)));
                ASSERT_SYS_OK(dup2(fd[1], send_pipe_num(j, i)));


                if(fd[0] != recv_pipe_num(j, i)){
                    ASSERT_SYS_OK(close(fd[0]));

                }
                if(fd[1] != send_pipe_num(j, i)){
                    ASSERT_SYS_OK(close(fd[1]));
                }
            }
        }
    }
    
    char buffer[4];
    int ret = snprintf(buffer, sizeof buffer, "%d", copies);
    if (ret < 0 || ret >= (int)sizeof(buffer))
        fatal("snprintf failed");
    setenv("MIMPI_number_of_processes", buffer, sizeof(int));
    pid_t pid;
    for(int i = 0; i < copies; i ++){
        ASSERT_SYS_OK(pid = fork());
        if(pid == 0){
            char buffer[4];
            int ret = snprintf(buffer, sizeof buffer, "%d", i);
            if (ret < 0 || ret >= (int)sizeof(buffer))
                fatal("snprintf failed");
            setenv("MIMPI_rank", buffer, sizeof(int));
            ASSERT_SYS_OK(execvp(path, args));
        }
    }
    
    for(int i = 0; i < copies; i ++){
        for(int j = 0; j < copies; j ++)
            if(i != j){
                ASSERT_SYS_OK(close(recv_pipe_num(j, i)));
                ASSERT_SYS_OK(close(send_pipe_num(j, i)));
            }
    }
    int status;
    for(int i = 0; i < copies; i ++) {
        ASSERT_SYS_OK(wait(&status));
    }
    
    
}
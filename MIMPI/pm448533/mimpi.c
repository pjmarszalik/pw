/**
 * This file is for implementation of MIMPI library.
 * */

#include "channel.h"
#include "mimpi.h"
#include "mimpi_common.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <malloc.h>
#include <string.h>

pthread_t thread[16];

void MIMPI_Init(bool enable_deadlock_detection) {
    channels_init();

    char * n = getenv("MIMPI_number_of_processes");
    char * my_id = getenv("MIMPI_rank");
    copies = atoi(n);
    id = atoi(my_id);

    ASSERT_SYS_OK(pthread_mutex_init(&mimpi_mutex, NULL));
    ASSERT_SYS_OK(pthread_cond_init(&proc_wait, NULL));

    q = (queue *)malloc(sizeof(queue));
    ASSERT_SYS_OK(pthread_mutex_init(&q -> mutex, NULL));
    q -> head = NULL;
    q -> tail = NULL;

    for(int i = 0; i < copies; i ++){
        for(int j = 0; j < copies; j ++)
            if(i != j && i != id){
                ASSERT_SYS_OK(close(recv_pipe_num(j, i)));
            }
    }

    for(int i = 0; i < copies; i ++){
        for(int j = 0; j < copies; j ++)
            if(i != j && j != id){
                ASSERT_SYS_OK(close(send_pipe_num(j, i)));
            }
    }  

    alive = (bool *)malloc(copies * sizeof(bool));

    for(int i = 0; i < copies; i ++){
        alive[i] = true;
        if(i != id){
            int *x = (int *)malloc(sizeof(int));
            *x = i;
            ASSERT_SYS_OK(pthread_create(&thread[i], NULL, reciever, x));
        }
    }
}

void MIMPI_Finalize() {

    for(int i = 0; i < copies; i ++){
        if(id != i){
            ASSERT_SYS_OK(close(send_pipe_num(id, i)));
        }
    }

    for(int i = 0; i < copies; i++){
        if(id != i)
            ASSERT_SYS_OK(pthread_join(thread[i], NULL));
    }

    for(int j = 0; j < copies; j ++){
        if(id != j){
            ASSERT_SYS_OK(close(recv_pipe_num(j, id)));
        }
    }
    
    ASSERT_SYS_OK(pthread_mutex_destroy(&mimpi_mutex));
    ASSERT_SYS_OK(pthread_cond_destroy(&proc_wait));
    ASSERT_SYS_OK(pthread_mutex_destroy(&q -> mutex));
    free(alive);
    node * current = q -> head;
    node * next;
    while(current != NULL){
        next = current -> next;
        remove_node(q, current);
        current = next;
    }
    free(q);
    channels_finalize();
}

int MIMPI_World_size() {
    return copies;
}

int MIMPI_World_rank() {
    return id;
}

MIMPI_Retcode MIMPI_Send(
    void const *data,
    int count,
    int destination,
    int tag
) {
    if(destination == id)
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;

    if(destination < 0 || copies <= destination)
        return MIMPI_ERROR_NO_SUCH_RANK;

    info * metadata = (info *)malloc(sizeof(info));
    metadata -> count = count;
    metadata -> tag = tag;
    metadata -> source = id;
    size_t length = sizeof(info) + count;
    void * container = (void *)malloc(length);
    memcpy(container, metadata, sizeof(info));
    memcpy(container + sizeof(info), data, count);
    long long pointer = 0;
    size_t scan;
    while(length > 0){
        if(length > PIPE_SIZE)
            scan = PIPE_SIZE;
        else
            scan = length;
        long long ret = chsend(send_pipe_num(id, destination), container + pointer, scan);
        if(ret == -1){
            if(errno == EPIPE){
                free(metadata);
                free(container);
                return MIMPI_ERROR_REMOTE_FINISHED;
            }
        }
        pointer += ret;
        length -= ret;
    }
    free(metadata);
    free(container);
    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Recv(
    void *data,
    int count,
    int source,
    int tag
) {
    if(source ==  id)
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;

    if(source < 0 || copies <= source)
        return MIMPI_ERROR_NO_SUCH_RANK;

    ASSERT_SYS_OK(pthread_mutex_lock(&mimpi_mutex));
    while(!find_msg(q, count, source, tag, data)){
        if(!alive[source]) {
            ASSERT_SYS_OK(pthread_mutex_unlock(&mimpi_mutex));
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        ASSERT_SYS_OK(pthread_cond_wait(&proc_wait, &mimpi_mutex));
    }
    
    ASSERT_SYS_OK(pthread_mutex_unlock(&mimpi_mutex));
    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Barrier() {
    if(copies == 1)
        return MIMPI_SUCCESS;

    int ret;
    char * barrier;
    int count = 1;
    if(id + 1 == 1){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, left(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, right(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count, left(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = "w";
        ret = MIMPI_Send(barrier, count, right(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else if(right(0) != -1){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, left(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, right(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count, up(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, up(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count, left(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = "w";
        ret = MIMPI_Send(barrier, count, right(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else if(left(0) != -1){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, left(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count, up(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, up(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);
        
        barrier = "w";
        ret = MIMPI_Send(barrier, count, left(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;        
    } else{
        barrier = "w";
        ret = MIMPI_Send(barrier, count, up(0), -1);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count, up(0), -1);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);
    }

    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Bcast(
    void *data,
    int count,
    int root
) {
    
    if(root < 0 || copies <= root)
        return MIMPI_ERROR_NO_SUCH_RANK;
    if(copies == 1)
        return MIMPI_SUCCESS;

    int ret;
    char * barrier;
    int count_barrier = 1;

    if(id == root){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, left(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, right(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        ret = MIMPI_Send(data, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        ret = MIMPI_Send(data, count, right(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

    } else if(right(root) != -1){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, left(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, right(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        ret = MIMPI_Recv(data, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        ret = MIMPI_Send(data, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        ret = MIMPI_Send(data, count, right(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else if(left(root) != -1){
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, left(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        ret = MIMPI_Recv(data, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        ret = MIMPI_Send(data, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else{
        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        ret = MIMPI_Recv(data, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    }
    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Reduce(
    void const *send_data,
    void *recv_data,
    int count,
    MIMPI_Op op,
    int root
) {
    if(root < 0 || copies <= root)
        return MIMPI_ERROR_NO_SUCH_RANK;

    if(copies == 1)
        return MIMPI_SUCCESS;

    int ret;
    char * barrier;
    int count_barrier = 0;
    uint8_t buffer1[count];
    uint8_t buffer2[count]; 
    uint8_t message2;
    uint8_t message1;
    if(id == root){
        
        ret = MIMPI_Recv(buffer1, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        

        if(copies >= 3){
            ret = MIMPI_Recv(buffer2, count, right(root), -2);
            if(ret == 3)
                return MIMPI_ERROR_REMOTE_FINISHED;
        }
        
        for(int i = 0; i < count; i++){
            message1 = buffer1[i];
            message2 = buffer2[i];
            if(op == 0 && message1 > message2)
                message2 = message1;
            else if(op == 1 && message1 < message2)
                message2 =  message1;
            else if(op == 2)
                message2 += message1;
            else if(op == 3)
                message2 *= message1;
            buffer2[i] = message2;
        }

        memcpy(buffer1, send_data, count);

        for(int i = 0; i < count; i++){
            message1 = buffer1[i];
            message2 = buffer2[i];
            if(op == 0 && message1 > message2)
                message2 = message1;
            else if(op == 1 && message1 < message2)
                message2 =  message1;
            else if(op == 2)
                message2 += message1;
            else if(op == 3)
                message2 *= message1;
            buffer2[i] = message2;
        }

        memcpy(recv_data, buffer2, count);

        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        if(copies >= 3){
            barrier = "w";
            ret = MIMPI_Send(barrier, count_barrier, right(root), -2);
            if(ret == 3)
                return MIMPI_ERROR_REMOTE_FINISHED;
        }
    } else if(right(root) != -1){
        ret = MIMPI_Recv(buffer1, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        ret = MIMPI_Recv(buffer2, count, right(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        for(int i = 0; i < count; i++){
            message1 = buffer1[i];
            message2 = buffer2[i];
            if(op == 0 && message1 > message2)
                message2 = message1;
            else if(op == 1 && message1 < message2)
                message2 =  message1;
            else if(op == 2)
                message2 += message1;
            else if(op == 3)
                message2 *= message1;
            buffer2[i] = message2;
        }

        memcpy(buffer1, send_data, count);

        for(int i = 0; i < count; i++){
            message1 = buffer1[i];
            message2 = buffer2[i];
            if(op == 0 && message1 > message2)
                message2 = message1;
            else if(op == 1 && message1 < message2)
                message2 =  message1;
            else if(op == 2)
                message2 += message1;
            else if(op == 3)
                message2 *= message1;
            buffer2[i] = message2;
        }

        ret = MIMPI_Send(buffer2, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, up(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, right(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else if(left(root) != -1){
        ret = MIMPI_Recv(buffer1, count, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;

        memcpy(buffer2, send_data, count);
        
        for(int i = 0; i < count; i++){
            message1 = buffer1[i];
            message2 = buffer2[i];
            if(op == 0 && message1 > message2)
                message2 = message1;
            else if(op == 1 && message1 < message2)
                message2 =  message1;
            else if(op == 2)
                message2 += message1;
            else if(op == 3)
                message2 *= message1;
            buffer2[i] = message2;
        }

        ret = MIMPI_Send(buffer2, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, up(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);

        barrier = "w";
        ret = MIMPI_Send(barrier, count_barrier, left(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
    } else{
        memcpy(buffer1, send_data, count);
        ret = MIMPI_Send(buffer1, count, up(root), -2);
        if(ret == 3)
            return MIMPI_ERROR_REMOTE_FINISHED;
        
        barrier = (void *)malloc(1);
        ret = MIMPI_Recv(barrier, count_barrier, up(root), -2);
        if(ret == 3){
            free(barrier);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        free(barrier);
    }

    return MIMPI_SUCCESS;
}
/**
 * This file is for implementation of common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#include "mimpi_common.h"
#include "mimpi.h"
#include "channel.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <malloc.h>

#define BUF_SIZE 512;

_Noreturn void syserr(const char* fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, " (%d; %s)\n", errno, strerror(errno));
    exit(1);
}

_Noreturn void fatal(const char* fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    fprintf(stderr, "\n");
    exit(1);
}

int id;
int copies;
pthread_cond_t proc_wait;
pthread_mutex_t mimpi_mutex;
queue * q;
bool * alive;

int send_pipe_num(int from, int to){
    return (20 + copies * copies + copies * to + from);
}
int recv_pipe_num(int from, int to){
    return (20 + copies * to + from);
}

int left(int root){
    int new_id = (id + copies - root) % copies + 1;
    int result = 2 * new_id;
    if(copies < result)
        return -1;
    result -= 1;
    result += root;
    result %= copies;
    return result;
}
int right(int root){
    int new_id = (id + copies - root) % copies + 1;
    int result = 2 * new_id + 1;
    if(copies < result)
        return -1;
    result -= 1;
    result += root;
    result %= copies;
    return result;
}
int up(int root){
    int new_id = (id + copies - root) % copies + 1;
    if(new_id == 0)
        return -1;
    int result = new_id / 2;
    
    return (result - 1 + root) % copies;
}

void push(queue * q, msg * msg){
    ASSERT_SYS_OK(pthread_mutex_lock(&q->mutex));

    node * new_node = (node *)malloc(sizeof(node));
    new_node -> msg = msg;
    node * helper = q -> tail;

    if(helper != NULL)
        helper -> next = new_node;
    else
        q -> head = new_node;

    new_node -> next = NULL;
    new_node -> prev = helper;
    q -> tail = new_node;
    ASSERT_SYS_OK(pthread_mutex_unlock(&q->mutex));
}



void remove_node(queue * q, node * good_node){
    node * prev = good_node -> prev;
    node * next = good_node -> next;
    if(prev != NULL){
        prev -> next = next;
    }
    else{
        q -> head = next;
    }

    if(next != NULL){
        next -> prev = prev;
    }
    else{
        q -> tail = prev;
    }

    free(good_node -> msg -> info);
    free(good_node -> msg -> data);
    free(good_node -> msg);
    free(good_node);
}

bool find_msg(queue * q, int count, int source, int tag, void * data){  
    ASSERT_SYS_OK(pthread_mutex_lock(&q->mutex));
    node * current = q -> head;
    while(current != NULL){
        if(current -> msg -> info -> source == source &&
            current -> msg -> info -> count == count &&
            (current -> msg -> info -> tag == tag || (current -> msg -> info -> tag >= 0 && tag == MIMPI_ANY_TAG))){
            memcpy(data, current -> msg -> data, count);
            remove_node(q, current);
            ASSERT_SYS_OK(pthread_mutex_unlock(&q->mutex));
            return true;
        }
        current = current -> next;
    }
    ASSERT_SYS_OK(pthread_mutex_unlock(&q->mutex));
    return false;
}

void * reciever(void * args){
    int source = *(int*)args;
    int read_bytes = 0;
    info * metadata;
    void * meta = (void *)malloc(sizeof(info));
    void * container;
    int to_read = 0;
    int offset = 0;
    int scan = 0;
    while(true){
        read_bytes = chrecv(recv_pipe_num(source, id), meta, sizeof(info));
        if(read_bytes == 0 || read_bytes == -1)
            break;
        metadata = (info *)malloc(sizeof(info));
        memcpy(metadata, meta, sizeof(info));
        to_read = metadata -> count;
        container = (void *)malloc(to_read);
        offset = 0;

        while(to_read > 0){
            if(to_read > PIPE_SIZE)
                scan = PIPE_SIZE;
            else
                scan = to_read;
            
            read_bytes = chrecv(recv_pipe_num(source, id), container + offset, scan);
            if(read_bytes == 0 || read_bytes == -1){
                free(metadata);
                free(container);
                break;
            }
                
            to_read -= read_bytes;
            offset += read_bytes;
        }
        if(read_bytes == 0 || read_bytes == -1){
            free(metadata);
            free(container);
            break;
        }
            

        msg * new_msg = (msg *)malloc(sizeof(msg));
        new_msg -> info = metadata;
        new_msg -> data = container;
            
        push(q, new_msg);

        ASSERT_SYS_OK(pthread_cond_signal(&proc_wait));
        
    }
    ASSERT_SYS_OK(pthread_mutex_lock(&mimpi_mutex));
    alive[source] = false;
    ASSERT_SYS_OK(pthread_mutex_unlock(&mimpi_mutex));
    ASSERT_SYS_OK(pthread_cond_signal(&proc_wait));

    free(meta);
    free(args);
    return NULL;
}
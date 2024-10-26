/**
 * This file is for declarations of  common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#ifndef MIMPI_COMMON_H
#define MIMPI_COMMON_H

#include <assert.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <semaphore.h>
#include <pthread.h>

/*
    Assert that expression doesn't evaluate to -1 (as almost every system function does in case of error).

    Use as a function, with a semicolon, like: ASSERT_SYS_OK(close(fd));
    (This is implemented with a 'do { ... } while(0)' block so that it can be used between if () and else.)
*/
#define ASSERT_SYS_OK(expr)                                                                \
    do {                                                                                   \
        if ((expr) == -1)                                                                  \
            syserr(                                                                        \
                "system command failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ", \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Assert that expression evaluates to zero (otherwise use result as error number, as in pthreads). */
#define ASSERT_ZERO(expr)                                                                  \
    do {                                                                                   \
        int const _errno = (expr);                                                         \
        if (_errno != 0)                                                                   \
            syserr(                                                                        \
                "Failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ",                \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Prints with information about system error (errno) and quits. */
_Noreturn extern void syserr(const char* fmt, ...);

/* Prints (like printf) and quits. */
_Noreturn extern void fatal(const char* fmt, ...);

#define TODO fatal("UNIMPLEMENTED function %s", __PRETTY_FUNCTION__);
#define PIPE_SIZE 4096



/////////////////////////////////////////////
// Put your declarations here



typedef struct info{
    int count;
    int tag;
    int source;
}info;

typedef struct msg{
    info * info;
    void * data;
}msg;

typedef struct node{
    struct node *prev, *next;
    struct msg * msg;
}node;

typedef struct queue{
    node *head, *tail;
    pthread_mutex_t mutex;
}queue;

void * reciever(void * args);
bool find_msg(queue * q, int source, int count, int tag, void * data);
int send_pipe_num(int from, int to);
int recv_pipe_num(int from, int to);
int left(int root);
int right(int root);
int up(int root);
void remove_node(queue * q, node * good_node);

extern int id;
extern int copies;
extern pthread_cond_t proc_wait;
extern pthread_mutex_t mimpi_mutex;
extern queue * q;
extern bool * alive;


#endif // MIMPI_COMMON_H
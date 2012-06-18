/*
 * Copyright (c) 2012 Linux Box Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "fchan.h"
#include "bchan.h"

#include <rpc/svc_rqst.h>

#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include <sys/signal.h>


#define FREE_FCHAN_MSG_NONE     0x0000
#define FREE_FCHAN_MSG_FREESELF 0x0001

static void
free_fchan_msg(fchan_msg *msg, unsigned int flags)
{
    if (!msg)
        return;
    free(msg->msg1);
    free(msg->msg2);
    if (flags & FREE_FCHAN_MSG_FREESELF)
        free(msg);
    return;
}

static void
free_fchan_res(fchan_res *res, unsigned int flags)
{
    if (!res)
        return;
    free(res->msg1);
    if (flags & FREE_FCHAN_MSG_FREESELF)
        free(res);
    return;
}

static int n_threads = 1;
static char *server_host = NULL;
static struct timeval timeout, default_timeout = { 120, 0 };

static uint32_t bchan_id;
static pthread_t* fchan_tid;
static int forechan_shutdown = FALSE;
static int always_destroy_client = FALSE;

void
thread_delay_s(int s)
{
    time_t now;
    struct timespec then;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

    now = time(0);
    then.tv_sec = now + s;
    then.tv_nsec = 0;
    
    pthread_mutex_lock(&mtx);
    pthread_cond_timedwait(&cv, &mtx, &then);
    pthread_mutex_unlock(&mtx);
}

void fchan_sighand(int sig)
{
    int code = 0;

    /* signal shutdown forechannel */
    forechan_shutdown = TRUE;
}

static void
fchan_signals()
{
    sigset_t mask, newmask;
    sigemptyset(&newmask);
    sigaddset(&newmask, SIGPIPE);
    pthread_sigmask(SIG_SETMASK, &newmask, &mask);

    /* trap shutdown */
    signal(SIGTERM, fchan_sighand);
}

static inline CLIENT*
fchan_create_client(void)
{
    CLIENT *cl;
    cl = clnt_create(server_host, FCHAN_PROG, FCHANV, "tcp");
    return (cl);
}

static void*
fchan_call_thread(void *arg)
{
    uint32_t *thr_ix = (uint32_t *) arg;
    fchan_res result_1;
    fchan_msg sendmsg1_1_arg;
    enum clnt_stat retval_1;
    CLIENT *cl = NULL;

    sendmsg1_1_arg.seqnum = 0;
    sendmsg1_1_arg.msg1 = strdup("hello");
    sendmsg1_1_arg.msg2 = strdup("it's me again");

    while (1) {

        /* exit if signalled */
        if (forechan_shutdown)
            break;

        if (!cl) {
            cl = fchan_create_client();
            if (! cl) {
                clnt_pcreateerror(server_host);
                exit(1);
            }
        }

	sendmsg1_1_arg.seqnum++;
	
	/* XDR's encode and decode routines will only
	 * allocate memory if the relevant destination pointer
	 * is NULL */
	memset(&result_1, 0, sizeof(fchan_res));
    
	retval_1 = sendmsg1_1(&sendmsg1_1_arg, &result_1, cl);
	if (retval_1 != RPC_SUCCESS) {
	    clnt_perror (cl, "call failed");
	}

	printf("result: msg1: %s\n", result_1.msg1);
	free_fchan_res(&result_1, FREE_FCHAN_MSG_NONE);

        if (always_destroy_client) {
            clnt_destroy(cl);
            cl = NULL;
        }
#if RAND_DELAY
	/* delay 1s (wont appear to be lockstep) */
	thread_delay_s(1);
#endif
    }
}

int
main (int argc, char *argv[])
{
    int opt, r, ix;

    while ((opt = getopt(argc, argv, "h:t:n:d")) != -1) {
        switch (opt) {
        case 'h':
            server_host = optarg;
            break;
        case 't':
            timeout.tv_sec = atol(optarg);
            break;
        case 'n':
            n_threads = atoi(optarg);
            break;
        case 'd':
            always_destroy_client = TRUE;
            break;
        default:
            break;
        }
    }

    if (! server_host) {
        printf ("usage: %s -h server_host [-n client_threads (default 1)] "
                "[-d (destroy clients continuously)]\n",
                argv[0]);
        return (EXIT_FAILURE);
    }

    fchan_signals();

    fchan_tid = (pthread_t *) malloc(n_threads * sizeof(pthread_t));
    for (ix = 0; ix < n_threads; ++ix) {
        r = pthread_create(&fchan_tid[ix], NULL, &fchan_call_thread, (void *) &ix);
    }

    for (ix = 0; ix < n_threads; ++ix) {
        r = pthread_join(fchan_tid[ix], NULL);
        printf("%s cleanup: pthread_join (fchan) ix %d result %d\n",
               argv[0], ix, r);
    }

    exit (0);
}

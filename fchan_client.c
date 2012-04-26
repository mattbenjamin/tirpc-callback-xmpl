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

extern void bchan_prog_1(struct svc_req *, register SVCXPRT *);

static uint32_t bchan_id;
static pthread_t fchan_tid;
static int forechan_shutdown = FALSE;

void fchan_sighand(int sig)
{
    int code = 0;

    /* signal shutdown forechannel */
    forechan_shutdown = TRUE;

    /* signal shutdown backchannel */
    code = svc_rqst_thrd_signal(bchan_id, SVC_RQST_SIGNAL_SHUTDOWN);
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

void
backchan_rpc_server(CLIENT *cl)
{
    SVCXPRT *xprt;
    svc_init_params svc_params;
    int code = 0;

    printf("Starting RPC service\n");

    /* New tirpc init function must (atm) be called to initialize the
     * library. */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.max_connections = 1024;
    svc_params.max_events = 300; /* don't know good values for this */
    svc_init(&svc_params);

    /* get a transport handle from our connected client
     * handle, cl is disposed for us */
    xprt = svc_vc_create_from_clnt(
        cl, 0 /* sendsz */, 0 /* recvsz */, SVC_VC_CREATE_FLAG_DPLX);

    if (!xprt) {
	fprintf(stderr, "%s\n", "Create SVCXPRT from CLIENT failed");
    }

    /* register service */
    if (!svc_register(xprt, BCHAN_PROG, BCHANV, bchan_prog_1, IPPROTO_TCP)) {
	fprintf (stderr, "%s", "unable to register (BCHAN_PROG, BCHANV, tcp).");
	exit(1);
    }

    /* Use SVC_VC_CREATE_FLAG_XPRT_NOREG, so xprt has no event channel */
    code = svc_rqst_new_evchan(&bchan_id,
                               NULL /* u_data */,
                               (SVC_VC_CREATE_FLAG_XPRT_NOREG |
                                SVC_RQST_FLAG_CHAN_AFFINITY));

    /* and bind xprt to it (it seems like the affinity flag belongs here,
     * rather than above) */
    code = svc_rqst_evchan_reg(bchan_id, xprt,
                               SVC_RQST_FLAG_CHAN_AFFINITY);

    /* service the backchannel */
    code = svc_rqst_thrd_run(bchan_id, SVC_RQST_FLAG_NONE);

    /* reclaim resources */
    svc_unregister(BCHAN_PROG, BCHANV); /* and free it? */

    return;
}

static void*
fchan_call_loop(void *arg)
{
    CLIENT *cl = (CLIENT *) arg;
    fchan_res result_1;
    fchan_msg sendmsg1_1_arg;
    enum clnt_stat retval_1;

    sendmsg1_1_arg.seqnum = 0;

    while (1) {

        /* exit if signalled */
        if (forechan_shutdown)
            break;

	sendmsg1_1_arg.seqnum++;
	sendmsg1_1_arg.msg1 = strdup("hello");
	sendmsg1_1_arg.msg2 = strdup("it's me again");
	
	/* XDR's encode and decode routines will only
	 * allocate memory if the relevant destination pointer
	 * is NULL */
	memset(&result_1, 0, sizeof(fchan_res));
    
	retval_1 = sendmsg1_1(&sendmsg1_1_arg, &result_1, cl);
	if (retval_1 != RPC_SUCCESS) {
	    clnt_perror (cl, "call failed");
	}

	printf("result: msg1: %s\n", result_1.msg1);

	free_fchan_msg(&sendmsg1_1_arg, FREE_FCHAN_MSG_NONE);

	/* delay 1s (wont appear to be lockstep) */
	thread_delay_s(1);
    }

    clnt_destroy (cl);
}


int
main (int argc, char *argv[])
{
    char *host;
    CLIENT *cl, *cl_backchan;
    enum clnt_stat retval_1;
    int r;
    
    if (argc < 2) {
        printf ("usage: %s server_host\n", argv[0]);
        exit (1);
    }
    host = argv[1];

    fchan_signals();

    cl = clnt_create(host, FCHAN_PROG, FCHANV, "tcp");
    if (cl == NULL) {
        clnt_pcreateerror (host);
        exit (1);
    }

    /* create a dedicated connection for the backchan */
    cl_backchan = clnt_create(host, FCHAN_PROG, FCHANV, "tcp");
    if (cl_backchan == NULL) {
        clnt_pcreateerror (host);
        exit (1);
    }

    /* call BIND_CONN_TO_SESSION equivalent RPC */
    retval_1 = bind_conn_to_session1_1(NULL, &r, cl_backchan);
    if (retval_1 != RPC_SUCCESS) {
        clnt_perror (cl_backchan, "call failed2");
    }

    /* start forward call loop using cl */
    r = pthread_create(&fchan_tid, NULL, &fchan_call_loop, (void*) cl);

    /* switch client to server endpoint */
    backchan_rpc_server(cl_backchan);

    r = pthread_join(fchan_tid, NULL);
    printf("%s cleanup: pthread_join (fchan) result %d\n", argv[0], r);

    exit (0);
}

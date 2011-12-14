#include <pthread.h>
#include "fchan.h"
#include "bchan.h"

void
thread_delay_s(int s)
{
    time_t now;
    struct timespec then;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

    now = time(0);
    then.tv_sec = now + 5;
    then.tv_nsec = 0;
    
    pthread_mutex_lock(&mtx);
    pthread_cond_timedwait(&cv, &mtx, &then);
    pthread_mutex_unlock(&mtx);
}

static void *
fchan_callbackthread(void *arg)
{
    SVCXPRT *xprt = (SVCXPRT *) arg;
    enum clnt_stat retval_1;
    bchan_res result_1;
    bchan_msg callback1_1_arg;
    CLIENT *cl;

    printf("fchan_callbackthread started\n");

    /* convert xprt to a dedicated client channel */
    cl = svc_vc_clnt_create(xprt, BCHAN_PROG, BCHANV,
			    SVC_VC_CLNT_CREATE_DEDICATED);

    callback1_1_arg.seqnum = 0;

    while (1) {

	/* arrange to delay every 5s */
	thread_delay_s(5);
	printf("fchan_callbackthread wakeup\n");

	callback1_1_arg.seqnum++;
	callback1_1_arg.msg1 = strdup("holla");
	callback1_1_arg.msg2 = strdup("back");
	
	/* XDR's encode and decode routines will only
	 * allocate memory if the relevant destination pointer
	 * is NULL */
	memset(&result_1, 0, sizeof(bchan_res));

	retval_1 = callback1_1(&callback1_1_arg, &result_1, cl);
	if (retval_1 != RPC_SUCCESS) {
	    printf("callback failed--client may be gone, thread return\n");
            goto out;
	}

	printf("result: msg1: %s msg2: %s\n", result_1.msg1);

    }

out:
    return;

} /* fchan_callbackthread */

bool_t
sendmsg1_1_svc(fchan_msg *argp, fchan_res *result, struct svc_req *rqstp)
{
    bool_t retval = TRUE;

    printf("svc rcpt fchan_msg msg1: %s msg2: %s seqnum: %d\n",
	   argp->msg1, argp->msg2, argp->seqnum);

    result->result = 0;
    result->msg1 = strdup("freebird");

    return (retval);
}

bool_t
bind_conn_to_session1_1_svc(void *argp, int *result, struct svc_req *rqstp)
{
    int r;
    pthread_t tid;
    SVCXPRT *xprt = rqstp->rq_xprt;

    /*
     * when we receive this call, we may convert the svc
     * transport handle to a client, and call on the backchannel
     */
    r = pthread_create(&tid, NULL, &fchan_callbackthread, (void*) xprt);
    if (! r)
        pthread_detach(tid);

    return ( (r) ? FALSE : TRUE );
}

bool_t
read_1_svc(read_args *args, read_res *res, struct svc_req *rqstp)
{
    bool_t retval = TRUE;

    printf("svc rcpt read seqnum: %d fileno %d off %d len %d flags %d\n",
	   args->seqnum,
           args->fileno,
           args->off,
           args->len,
           args->flags);

    memset(res, 0, sizeof(read_res));

    switch (args->flags) {
    case 0:
        /* in this case, send a pattern */
        res->flags = 0;
        res->data.data_len = 32768;
        res->data.data_val = malloc(32768 * sizeof(char));
        sprintf(res->data.data_val, "%d %d", args->off, args->len);
        res->eof = 0;
        break;
    default:
        res->eof = 1;
    }

    return (retval);
}

bool_t
write_1_svc(write_args *args, int *res, struct svc_req *rqstp)
{
    bool_t retval = TRUE;

    printf("svc rcpt write seqnum: %d fileno %d off %d len %d flags %d\n",
	   args->seqnum,
           args->fileno,
           args->off,
           args->len,
           args->flags);

    /* do something with data */

    *res = 0;

    return (retval);
}

int
fchan_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    xdr_free (xdr_result, result);
    
    /*
     * Insert additional freeing code here, if needed
     */
    
    return (1);
}

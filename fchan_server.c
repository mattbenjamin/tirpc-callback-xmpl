#include "fchan.h"
#include "bchan.h"

#include <unistd.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "duplex_unit.h"

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

int
read_1_svc_callback(struct svc_req *rqstp)
{

    return (0);
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

    /* in this case, send a pattern */
    res->flags = 0;
    res->data.data_len = 32768;
    res->data.data_val = malloc(32768 * sizeof(char));
    sprintf(res->data.data_val, "%d %d", args->off, args->len);

    if (args->flags & DUPLEX_UNIT_IMMED_CB) {
        printf("callback!\n");
    }

    return (retval);
}

bool_t
write_1_svc(write_args *args, write_res *res, struct svc_req *rqstp)
{
    bool_t retval = TRUE;

    printf("svc rcpt write seqnum: %d fileno %d off %d len %d flags %d\n",
	   args->seqnum,
           args->fileno,
           args->off,
           args->len,
           args->flags);

    /* do something with data */

    memset(res, 0, sizeof(write_res));

    return (retval);
}

#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif

/* !! Regenerate in fchan_svc.c */

static void
fchan_prog_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
	union {
		fchan_msg sendmsg1_1_arg;
		read_args read_1_arg;
		write_args write_1_arg;
	} argument;
	union {
		fchan_res sendmsg1_1_res;
		int bind_conn_to_session1_1_res;
		read_res read_1_res;
		write_res write_1_res;
	} result;
	bool_t retval;
	xdrproc_t _xdr_argument, _xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply (transp, (xdrproc_t) xdr_void, (char *)NULL);
		return;

	case SENDMSG1:
		_xdr_argument = (xdrproc_t) xdr_fchan_msg;
		_xdr_result = (xdrproc_t) xdr_fchan_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))sendmsg1_1_svc;
		break;

	case BIND_CONN_TO_SESSION1:
		_xdr_argument = (xdrproc_t) xdr_void;
		_xdr_result = (xdrproc_t) xdr_int;
		local = (bool_t (*) (char *, void *,  struct svc_req *))bind_conn_to_session1_1_svc;
		break;

	case READ:
		_xdr_argument = (xdrproc_t) xdr_read_args;
		_xdr_result = (xdrproc_t) xdr_read_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))read_1_svc;
		break;

	case WRITE:
		_xdr_argument = (xdrproc_t) xdr_write_args;
		_xdr_result = (xdrproc_t) xdr_write_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))write_1_svc;
		break;

	default:
		svcerr_noproc (transp);
		return;
	}
	memset ((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
		svcerr_decode (transp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(transp, (xdrproc_t) _xdr_result, (char *)&result)) {
		svcerr_systemerr (transp);
	}
	if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
		fprintf (stderr, "%s", "unable to free arguments");
		exit (1);
	}
	if (!fchan_prog_1_freeresult (transp, _xdr_result, (caddr_t) &result))
		fprintf (stderr, "%s", "unable to free results");

	return;
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

SVCXPRT *xprt;
int server_port;

#define FCHAN_SVC_UDP 0x0001
#define FCHAN_SVC_TCP 0x0002

static int
fchan_server_create(unsigned int flags)
{
    svc_init_params svc_params;
    struct sockaddr_in saddr;
    struct t_bind bindaddr; /* XXX expected by svc_tli_create  */
    int code, fd;

    printf("Starting RPC service\n");

    /* New tirpc init function must be called to initialize the
     * library. */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.max_connections = 1024;
    svc_params.max_events = 300; /* don't know good values for this */
    svc_init(&svc_params);

    pmap_unset (FCHAN_PROG, FCHANV);

    switch (server_port) {
    case 0:
        if (flags & FCHAN_SVC_UDP) {
            xprt = svcudp_create(RPC_ANYSOCK);
            if (xprt == NULL) {
                fprintf (stderr, "%s", "cannot create udp service.");
                exit(1);
            }
        }
	xprt = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (xprt == NULL) {
            fprintf (stderr, "%s", "cannot create tcp service.");
            exit(1);
	}
    default:
        /* bind an explicit port */
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == -1) {
            exit(1);
        }

        memset(&saddr, 0, sizeof(struct sockaddr_in));

        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(server_port);
        saddr.sin_addr.s_addr = INADDR_ANY;

        bindaddr.addr.buf = &saddr;
        bindaddr.addr.maxlen =
            bindaddr.addr.len = sizeof(struct sockaddr_in);
        bindaddr.qlen = 10;
        
        xprt = svc_tli_create(
            fd, NULL /* nconf */, &bindaddr, 0 /* sendsz */, 0 /* recvsz */);
        if (! xprt) {
            perror("error svc_fd_create failed");
            exit(1);
        }
        break;
    } /* switch */

    if (flags & FCHAN_SVC_UDP) {
        if (!svc_register(xprt, FCHAN_PROG, FCHANV, fchan_prog_1,
                          IPPROTO_UDP)) {
            fprintf (stderr, "%s", "unable to register (FCHAN_PROG, FCHANV,"
                     " udp).");
            exit(1);
        }
    }
    if (!svc_register(xprt, FCHAN_PROG, FCHANV, fchan_prog_1,
                      IPPROTO_TCP)) {
        fprintf (stderr, "%s", "unable to register (FCHAN_PROG, FCHANV, "
                 "tcp).");
        exit(1);
    }

    return (0);
}

int
main (int argc, char **argv)
{
    int opt, code;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            server_port = atoi(optarg);
            break;
        default:
            break;
        }
    }

    if (! server_port) {
        printf ("usage: %s -p server_port\n", argv[0]);
        return (EXIT_FAILURE);
    }

    code = fchan_server_create(FCHAN_SVC_TCP);
    svc_run ();
    fprintf (stderr, "%s", "svc_run returned");
    exit (1);
    /* NOTREACHED */
}

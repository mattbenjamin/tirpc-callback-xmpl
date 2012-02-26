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
#include <sys/signal.h>
#include <netinet/in.h>

#include <rpc/svc_rqst.h>

#include "duplex_unit.h"

static uint32_t fchan_id;
static pthread_t fchan_cb_tid = (pthread_t) 0;
static CLIENT *duplex_clnt = NULL;
static int new_style_event_loop = FALSE;
static int override_getreq = FALSE;
static int signal_shutdown = FALSE;

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

static inline void
svc_set_rq_clntcred(struct svc_req *r, struct rpc_msg *msg)
{
    r->rq_clntcred = msg->rm_call.cb_cred.oa_base + MAX_AUTH_BYTES;
}

static bool_t
fchan_server_getreq(SVCXPRT *xprt)
{
  struct svc_req r;
  struct rpc_msg *msg;
  enum xprt_stat stat;
  sigset_t mask;
  bool_t destroyed;
  int xp_fd = xprt->xp_fd;

  msg = alloc_rpc_msg();
  svc_set_rq_clntcred(&r, msg);
  destroyed = FALSE;

  /* serialize xprt */
  vc_fd_lock(xp_fd, &mask /*, "duplex_unit_getreq" */);

  /* now receive msgs from xprt (support batch calls) */
  do
    {
      if (SVC_RECV (xprt, msg))
        {

         /* if msg->rm_direction=REPLY, another thread is waiting
          * to handle it */

            /* the goal is not force a control transfer in the common
             * case.  the likelihood can be reduced by updating the
             * epoll registration of xprt.xp_fd between around calls
             * and also around svc callouts */

            if (msg->rm_direction == REPLY)
                abort();

	  /* now find the exported program and call it */
          svc_vers_range_t vrange;
          svc_lookup_result_t lkp_res;
          svc_rec_t *svc_rec;
	  enum auth_stat why;

	  r.rq_xprt = xprt;
	  r.rq_prog = msg->rm_call.cb_prog;
	  r.rq_vers = msg->rm_call.cb_vers;
	  r.rq_proc = msg->rm_call.cb_proc;
	  r.rq_cred = msg->rm_call.cb_cred;

	  /* first authenticate the message */
	  if ((why = _authenticate (&r, msg)) != AUTH_OK)
	    {
	      svcerr_auth (xprt, why);
	      goto call_done;
	    }

          lkp_res = svc_lookup(&svc_rec, &vrange, r.rq_prog, r.rq_vers,
                               NULL, 0);
          switch (lkp_res) {
          case SVC_LKP_SUCCESS:
              (*svc_rec->sc_dispatch) (&r, xprt);
              goto call_done;
              break;
          SVC_LKP_VERS_NOTFOUND:
              svcerr_progvers (xprt, vrange.lowvers, vrange.highvers);
          default:
              svcerr_noprog (xprt);
              break;
          }
        } /* SVC_RECV again? probably want to try a non-blocking
           * recv or MSG_PEEK */

      /*
       * Check if the xprt has been disconnected in a
       * recursive call in the service dispatch routine.
       * If so, then break.
       */
      if (!svc_validate_xprt_list(xprt))
          break;

    call_done:
      /* XXX locking and destructive ops on xprt need to be reviewed */
      if ((stat = SVC_STAT (xprt)) == XPRT_DIED)
	{
            /* XXX the xp_destroy methods call the new svc_rqst_xprt_unregister
             * routine, so there shouldn't be internal references to xprt.  The
             * API client could also override this routine.  Still, there may
             * be a motivation for adding a lifecycle callback, since the API
             * client can get a new-xprt callback, and could have kept the
             * address (and should now be notified we are disposing it). */
            __warnx("%s: stat == XPRT_DIED (%p) \n", __func__, xprt);

            vc_fd_unlock(xp_fd, &mask /*, "duplex_unit_getreq" */);
            SVC_DESTROY (xprt);
            destroyed = TRUE;
            break;
	}
    else if ((xprt->xp_auth != NULL) &&
	     (xprt->xp_auth->svc_ah_private == NULL))
	{
            xprt->xp_auth = NULL;
	}
    }
  while (stat == XPRT_MOREREQS);

  free_rpc_msg(msg);

  if (! destroyed)
      vc_fd_unlock(xp_fd, &mask /*, "duplex_unit_getreq" */);
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
    cl = clnt_vc_create_from_svc(
        xprt,
        BCHAN_PROG, BCHANV,
        SVC_VC_CREATE_FLAG_SPLX | SVC_VC_CREATE_FLAG_DISPOSE);

    if (! cl) {
        printf("%s: clnt_vc_create_from_svc failed\n");
        goto out;
    }

    callback1_1_arg.seqnum = 0;
    callback1_1_arg.msg1 = strdup("holla");
    callback1_1_arg.msg2 = strdup("back");

    while (1) {

	/* arrange to delay every 5s */
	thread_delay_s(5);
	printf("fchan_callbackthread wakeup\n");

        if (signal_shutdown)
            break;

	callback1_1_arg.seqnum++;
	
	/* XDR's encode and decode routines will only
	 * allocate memory if the relevant destination pointer
	 * is NULL */
	memset(&result_1, 0, sizeof(bchan_res));

	retval_1 = callback1_1(&callback1_1_arg, &result_1, cl);
	if (retval_1 != RPC_SUCCESS) {
	    printf("callback failed--client may be gone, thread return\n");
            goto reclaim;
	}

	printf("result: msg1: %s msg2: %s\n", result_1.msg1);
        free(result_1.msg1);
    }

reclaim:
    free(callback1_1_arg.msg1);
    free(callback1_1_arg.msg2);

    /* reclaim resources */
    clnt_vc_destroy(cl);

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
    SVCXPRT *xprt = rqstp->rq_xprt;

    printf("svc rcpt bind_conn_to_session1_1\n");

    /*
     * when we receive this call, we may convert the svc
     * transport handle to a client, and call on the backchannel
     */
    r = pthread_create(&fchan_cb_tid, NULL, &fchan_callbackthread,
                       (void*) xprt);

    return ( (r) ? FALSE : TRUE );
}

/* Send a synchronous callback on the shared RPC channel. */
int
read_1_svc_callback(read_args *args, struct svc_req *rq)
{
    enum clnt_stat cl_stat;
    bchan_msg callback1_1_arg[1];
    bchan_res callback1_1_res[1];

    SVCXPRT *xprt = rq->rq_xprt;
    static struct timeval timeout = { /* 25 */ 120, 0 };

    /* convert xprt to a shared client channel */
    if (! duplex_clnt)
        duplex_clnt = clnt_vc_create_from_svc(
            xprt,
            BCHAN_PROG, BCHANV,
            SVC_VC_CREATE_FLAG_DPLX |SVC_VC_CREATE_FLAG_DISPOSE);

    fprintf(stderr, "read_1_svc_callback before call\n");

    callback1_1_arg->seqnum = 969;
    callback1_1_arg->msg1 = strdup("read_1_svc_callback");
    callback1_1_arg->msg2 = strdup("sync");

    cl_stat = clnt_call(duplex_clnt, CALLBACK1,
                        (xdrproc_t) xdr_bchan_msg, (caddr_t) callback1_1_arg,
                        (xdrproc_t) xdr_bchan_res, (caddr_t) callback1_1_res,
                        timeout);

    if (cl_stat != RPC_SUCCESS)
        clnt_perror(duplex_clnt, "callback1_1 failed");
    
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
        read_1_svc_callback(args, rqstp);
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

        /* XXXX valgrind warns these used uninitialized */
        memset(&argument, 0, sizeof(argument));
        memset(&result, 0, sizeof(result));

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

void fchan_sighand(int sig)
{
    int code = 0;

    /* signal shutdown forechannel */
    signal_shutdown = TRUE;

    /* signal shutdown backchannel */
    code = svc_rqst_thrd_signal(fchan_id, SVC_RQST_SIGNAL_SHUTDOWN);   
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

static int
forechan_rpc_server(unsigned int flags)
{
    svc_init_params svc_params;
    struct sockaddr_in saddr;
    struct t_bind bindaddr; /* XXX expected by svc_tli_create  */
    int code, one = 1, fd;

    printf("Starting RPC service\n");

    /* New tirpc init function must be called to initialize the
     * library. */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.max_connections = 1024;
    svc_params.max_events = 300; /* don't know good values for this */
    svc_init(&svc_params);

    pmap_unset (FCHAN_PROG, FCHANV);

    fchan_signals();

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

        /* more nicely support restarts */
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        memset(&saddr, 0, sizeof(struct sockaddr_in));

        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(server_port);
        saddr.sin_addr.s_addr = INADDR_ANY;

        bindaddr.addr.buf = &saddr;
        bindaddr.addr.maxlen =
            bindaddr.addr.len = sizeof(struct sockaddr_in);
        bindaddr.qlen = 10;
        
        xprt = svc_tli_create(fd,
                              NULL /* nconf */,
                              &bindaddr,
                              0 /* sendsz */,
                              0 /* recvsz */);
        if (! xprt) {
            perror("error svc_fd_create failed");
            exit(1);
        }

        if (override_getreq)
            code = SVC_CONTROL(xprt, SVCSET_XP_GETREQ, fchan_server_getreq);
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

    switch (new_style_event_loop) {
    case TRUE:
        code = svc_rqst_new_evchan(&fchan_id,
                                   NULL /* u_data */,
                                   SVC_RQST_FLAG_CHAN_AFFINITY);

        /* bind xprt to channel--unregister it from the global event
         * channel (if applicable) */
        code = svc_rqst_evchan_reg(fchan_id, xprt,
                                   SVC_RQST_FLAG_XPRT_UREG|
                                   SVC_RQST_FLAG_CHAN_AFFINITY);

        /* service the backchannel */
        code = svc_rqst_thrd_run(fchan_id, SVC_RQST_FLAG_NONE);

        break;
    default:
        svc_run();
        break;
    }

    /* delete event channel, unregisters all xprts */
    code = svc_rqst_delete_evchan(fchan_id, SVC_RQST_FLAG_NONE);
    if (code)
        printf("%s: svc_rqst_delete_evchan (%d) returned %d\n",
               __func__, fchan_id, code);

    /* unbind and clean up svc database */
    svc_unregister(FCHAN_PROG, FCHANV); /* and free it? */

    /* dispose xprt */
    SVC_DESTROY(xprt);

    return (0);
}

int
main (int argc, char **argv)
{
    int opt, code;

    while ((opt = getopt(argc, argv, "gnp:")) != -1) {
        switch (opt) {
        case 'g':
            override_getreq = TRUE;
            break;
        case 'n':
            new_style_event_loop = TRUE;
            break;
        case 'p':
            server_port = atoi(optarg);
            break;
        default:
            break;
        }
    }

    if (! server_port) {
        printf ("usage: %s [-n -g] -p server_port\n", argv[0]);
        return (EXIT_FAILURE);
    }

    code = forechan_rpc_server(FCHAN_SVC_TCP);
    printf("forechannel_rpc_server result %d\n", code);

    /* XXXX bug, need to recall all unjoined tids */
    if (fchan_cb_tid) {
        code = pthread_join(fchan_cb_tid, NULL);
        printf("%s cleanup: pthread_join (fchan) result %d\n", argv[0], code);
    }

    (void) svc_shutdown(SVC_SHUTDOWN_FLAG_NONE);

    exit (0);
    /* NOTREACHED */
}


#include <pthread.h>
#include <reentrant.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/signal.h>
#include <unistd.h>

#include "fchan.h"
#include "bchan.h"
#include <rpc/rpc.h>

#include "CUnit/Basic.h"

#include "duplex_unit.h"

/*
 *  BEGIN SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

char *server_host;
int server_port;
CLIENT *cl_duplex_chan;
SVCXPRT *duplex_xprt;
pthread_t bchan_tid;

static bool_t backchan_svc_started = FALSE;
static pthread_mutex_t backchan_svc_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t backchan_svc_cv = PTHREAD_COND_INITIALIZER;

void
post_backchan_start()
{
    pthread_mutex_lock(&backchan_svc_mtx);
    backchan_svc_started = TRUE;
    pthread_cond_broadcast(&backchan_svc_cv);
    pthread_mutex_unlock(&backchan_svc_mtx);
}

void
sync_backchan_start()
{
    bool_t locked;
    pthread_mutex_lock(&backchan_svc_mtx);
    locked = TRUE;

    if (backchan_svc_started)
        goto unlock;

    pthread_mutex_unlock(&backchan_svc_mtx);
    while (! backchan_svc_started) {
        pthread_mutex_lock(&backchan_svc_mtx);
        pthread_cond_wait(&backchan_svc_cv, &backchan_svc_mtx);
        pthread_mutex_unlock(&backchan_svc_mtx);
    }
    

unlock:
    pthread_mutex_unlock(&backchan_svc_mtx);
}

static struct timeval timeout, default_timeout = { 120, 0 };

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

static void
duplex_unit_signals()
{
    sigset_t mask, newmask;
    sigemptyset(&newmask);
    sigaddset(&newmask, SIGPIPE);
    pthread_sigmask(SIG_SETMASK, &newmask, &mask);
}

extern void bchan_prog_1(struct svc_req *, register SVCXPRT *);

bool_t
callback1_1_svc(bchan_msg *argp, bchan_res *result, struct svc_req *rqstp)
{

    bool_t retval = TRUE;

    printf("svc rcpt bchan_msg msg1: %s msg2: %s seqnum: %d\n",
	   argp->msg1, argp->msg2, argp->seqnum);

    result->result = 767;
    result->msg1 = strdup("bungee");

    return (retval);
}

int
bchan_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);

	/*
	 * Insert additional freeing code here, if needed
	 */

	return 1;
}

static inline void
svc_set_rq_clntcred(struct svc_req *r, struct rpc_msg *msg)
{
    r->rq_clntcred = msg->rm_call.cb_cred.oa_base + MAX_AUTH_BYTES;
}


static bool_t
duplex_unit_getreq(SVCXPRT *xprt)
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
            vc_fd_unlock(xp_fd, &mask /*, "duplex_unit_getreq" */);
#if 0 /* the case is interesting, but we don't need to double free xprt */
            SVC_DESTROY (xprt);
#endif
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

static void*
backchannel_rpc_server(void *arg)
{
    svc_init_params svc_params;
    CLIENT *cl;
    int code;

    printf("Starting RPC service\n");

    /* New tirpc init function must be called to initialize the
     * library. */
    svc_params.flags = SVC_INIT_EPOLL; /* use EPOLL event mgmt */
    svc_params.max_connections = 1024;
    svc_params.max_events = 300; /* don't know good values for this */
    svc_init(&svc_params);

    cl = (CLIENT *) arg;

    /* get a transport handle from our connected client
     * handle, cl is disposed for us */
    duplex_xprt = svc_vc_create_from_clnt(
        cl,
        0 /* sendsz */, 0 /* recvsz */,
        SVC_VC_CREATE_FLAG_DPLX);

    if (!duplex_xprt) {
	fprintf(stderr, "%s\n", "Create SVCXPRT from CLIENT failed");
    }

    /* install private getreq handler */
    code = SVC_CONTROL(duplex_xprt, SVCSET_XP_GETREQ, duplex_unit_getreq);

    /* register service */
    if (!svc_register(duplex_xprt, BCHAN_PROG, BCHANV, bchan_prog_1,
                      IPPROTO_TCP)) {
	fprintf (stderr, "%s", "unable to register (BCHAN_PROG, BCHANV, tcp).");
	exit(1);
    }

    post_backchan_start();

    /* service the backchannel */
    svc_run ();

    /* signalled */
    svc_unregister(BCHAN_PROG, BCHANV); /* and free it? */

    /* clean up */
    SVC_DESTROY(duplex_xprt);

    return (NULL);
}

static CLIENT*
duplex_unit_clnt_create(const char *host, const int port)
{
    struct sockaddr_in saddr;
    struct netbuf raddr;
    CLIENT *cl = NULL;
    int fd, code = 0;

    duplex_unit_signals();

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1)
        goto out;
 
    memset(&saddr, 0, sizeof(struct sockaddr_in));
 
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    code = inet_pton(AF_INET, host, &saddr.sin_addr);
    if (code <= 0) {
        fprintf(stderr, "%s (%d)\n", "Error returned from inet_pton", code);
        goto out;
    }

    /* cleanest to connect ourselves */
    code = connect(fd, (struct sockaddr *) &saddr, sizeof(struct sockaddr_in));
    if (code == -1) {
        fprintf(stderr, "%s (%s, port %d)\n", "Connect failed", host, port);
        goto out;
    }

    raddr.buf = &saddr;
    raddr.maxlen = raddr.len = sizeof(struct sockaddr_in);
    cl = clnt_vc_create2(fd, &raddr, FCHAN_PROG, FCHANV,
                         0 /* sendsz */,
                         0 /* recvsz */,
                         0 /* flags */);
out:
    return (cl);
}

static int
duplex_rpc_unit_PkgInit(int argc, char *argv[])
{
    int opt, r;

    server_host = NULL;
    cl_duplex_chan = NULL;

    timeout = default_timeout;

    while ((opt = getopt(argc, argv, "h:t:p:")) != -1) {
        switch (opt) {
        case 'h':
            server_host = optarg;
            break;
        case 'p':
            server_port = atoi(optarg);
            break;
        case 't':
            timeout.tv_sec = atol(optarg);
            break;
        default:
            break;
        }
    }

    if (! server_host) {
        printf ("usage: %s -h server_host -p server_port\n", argv[0]);
        return (EXIT_FAILURE);
    }

    cl_duplex_chan = duplex_unit_clnt_create(server_host, server_port);
    if (cl_duplex_chan == NULL) {
        clnt_pcreateerror(server_host);
        return (1);
    }

    /* start backchannel service using cl */
    r =  pthread_create(&bchan_tid, NULL, &backchannel_rpc_server,
                        (void*) cl_duplex_chan);

    sync_backchan_start();

    return CUE_SUCCESS;

} /* duplex_rpc_unit_PkgInit */

/* 
 * The suite initialization function.
 * Initializes resources to be shared across tests.
 * Returns zero on success, non-zero otherwise.
 *
 */
int init_suite1(void)
{

    return (0);
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite1(void)
{
    svc_exit(); /* post shutdown to the backchannel */
    pthread_join(bchan_tid, NULL);
    CLNT_DESTROY(cl_duplex_chan);

    return (0);
}

/* Tests */

#define FREE_READ_RES_NONE     0x0000
#define FREE_READ_RES_FREESELF 0x0001

static void
free_read_res(read_res *res, unsigned int flags)
{
    if (!res)
        return;
    free(res->data.data_val);
    if (flags & FREE_READ_RES_FREESELF)
        free(res);
    return;
}

/* Read 2^20 (1m) bytes in 32K blocks (2^5 count) */
void read_1m_1(void)
{
    int ix, code;
    enum clnt_stat cl_stat;
    read_args args[1];
    read_res res[1];

    /* setup args */
    args->seqnum = 0;
    args->off = 0;
    args->len = 32768;
    args->flags = 0;

    /* allocate -one- buffer for res */
    memset(res, 0, sizeof(read_res)); /* zero res pointer members */

    for (ix = 0; ix < 32; ++ix) {

        args->off = ix * args->len;

        cl_stat = clnt_call(cl_duplex_chan, READ,
                            (xdrproc_t) xdr_read_args, (caddr_t) args,
                            (xdrproc_t) xdr_read_res, (caddr_t) res,
                            timeout);

	if (cl_stat != RPC_SUCCESS) {
            char seqbuf[128];
            sprintf(seqbuf, "read_1m_1 %d failed", ix);
	    clnt_perror (cl_duplex_chan, seqbuf);
        }

        CU_ASSERT_EQUAL(cl_stat, RPC_SUCCESS);

        printf("read_1m_1: %s\n", res->data.data_val);
        if (res->eof)
            break;
    }

    free_read_res(res, FREE_READ_RES_NONE);

    return;
}

/* Write 2^20 (1m) bytes in 32K blocks (2^5 count) */
void write_1m_1(void)
{
    int ix, code, res;
    enum clnt_stat cl_stat;
    write_args args[1];

    res = 0;

    /* setup args */
    args->seqnum = 0;
    args->off = 0;
    args->len = 32768;
    args->data.data_len = args->len;
    args->data.data_val = malloc(32768 * sizeof(char));
    args->flags = 0;

    args->flags2 = 1;
    args->flags3 = 2;
    args->flags4 = 3;

    printf("\n");

    for (ix = 0; ix < 32; ++ix) {

        cl_stat = clnt_call(cl_duplex_chan, WRITE,
                            (xdrproc_t) xdr_write_args, (caddr_t) args,
                            (xdrproc_t) xdr_int, (caddr_t) &res,
                            timeout);

	if (cl_stat != RPC_SUCCESS) {
            char seqbuf[128];
            sprintf(seqbuf, "write_1m_1 %d failed", ix);
	    clnt_perror (cl_duplex_chan, seqbuf);
        }

        CU_ASSERT_EQUAL(cl_stat, RPC_SUCCESS);

        /* update buffer */
        args->off += 32768;
        args->seqnum++;

        args->flags2++;
        args->flags3++;
        args->flags4++;
    }

    free(args->data.data_val);
    return;
}

/* read 3 32K blocks at offset 0, tell the server to make an interleaved
 * backchannel call after block2 */
void read_3b_overlap_b2(void)
{
    int ix, code;
    enum clnt_stat cl_stat;
    read_args args[1];
    read_res res[1];

    /* setup args */
    args->seqnum = 0;
    args->off = 0;
    args->len = 32768;
    args->flags = 0;

    /* allocate one buffer for res */
    memset(res, 0, sizeof(read_res)); /* zero res pointer members */

    for (ix = 0; ix < 4; ++ix) {

        args->off = ix * args->len;

        args->flags = 0;
        if (ix == 2) {
            args->flags = DUPLEX_UNIT_IMMED_CB;
        }

        cl_stat = clnt_call(cl_duplex_chan, READ,
                            (xdrproc_t) xdr_read_args, (caddr_t) args,
                            (xdrproc_t) xdr_read_res, (caddr_t) res,
                            timeout);

	if (cl_stat != RPC_SUCCESS) {
            char seqbuf[128];
            sprintf(seqbuf, "read_1m_1 %d failed", ix);
	    clnt_perror (cl_duplex_chan, seqbuf);
        }

        CU_ASSERT_EQUAL(cl_stat, RPC_SUCCESS);

        printf("read_1m_1: %s\n", res->data.data_val);
        if (res->eof)
            break;
    }

    free_read_res(res, FREE_READ_RES_NONE);

    return;
}

void check_1(void)
{
    CU_ASSERT_EQUAL(0,0);
}

/* The main() function for setting up and running the tests.
 * Returns a CUE_SUCCESS after a successful run, another
 * CUnit error code on failure.
 */
int main(int argc, char *argv[])
{ 
    int32_t code = CUE_SUCCESS;

    /* initialize the CUnit test registry...  get this party started */
    if (CUE_SUCCESS != CU_initialize_registry())
       return CU_get_error();

    /* RPC Smoke Tests */
    CU_TestInfo rpc_smoke_tests[] = {
      { "Write 1m 1.", write_1m_1 },
      { "Read 1m 1.", read_1m_1 },
      { "Read 3 with async callback arrival after b2.", read_3b_overlap_b2 },
      { "Some check.", check_1 },
      CU_TEST_INFO_NULL,
    };

    /* More Tests */
    /* ... */

    /* Wire Up */
    CU_SuiteInfo suites[] = {
      { "Suite 1", init_suite1, clean_suite1,
	rpc_smoke_tests },
      CU_SUITE_INFO_NULL,
    };
  
    CU_ErrorCode error = CU_register_suites(suites);

    /* Initialize this package */
    code = duplex_rpc_unit_PkgInit(argc, argv);
    switch (code) {
    case CUE_SUCCESS:
        /* Run all tests using the CUnit Basic interface */
        CU_basic_set_mode(CU_BRM_VERBOSE);
        CU_basic_run_tests();
        CU_cleanup_registry();
        break;
    default:
        break;
    }

    return CU_get_error();
}

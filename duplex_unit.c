#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "fchan.h"
#include "bchan.h"

#include "CUnit/Basic.h"

#include "duplex_unit.h"

/*
 *  BEGIN SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

char *host;
CLIENT *cl_duplex_chan;
SVCXPRT *duplex_xprt;

static struct timeval timeout, default_timeout = { 25, 0 };

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

bool_t
callback1_1_svc(bchan_msg *argp, bchan_res *result, struct svc_req *rqstp)
{

    bool_t retval = TRUE;

    printf("svc rcpt bchan_msg msg1: %s msg2: %s seqnum: %d\n",
	   argp->msg1, argp->msg2, argp->seqnum);

    result->result = 0;
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

static void*
backchannel_rpc_server(void *arg)
{
    svc_init_params svc_params;
    CLIENT *cl;

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
    duplex_xprt = svc_vc_create_cl( cl,
                                    0 /* sendsize */, 
                                    0 /* recvsize */,
                                    SVC_VC_CLNT_CREATE_SHARED);
    if (!duplex_xprt) {
	fprintf(stderr, "%s\n", "Create SVCXPRT from CLIENT failed");
    }

    /* register service */
    if (!svc_register(duplex_xprt, BCHAN_PROG, BCHANV, bchan_prog_1,
                      IPPROTO_TCP)) {
	fprintf (stderr, "%s", "unable to register (BCHAN_PROG, BCHANV, tcp).");
	exit(1);
    }

    /* service the backchannel */
    svc_run (); /* XXX need something that supports shutdown */

    return (NULL);
}

static int
duplex_rpc_unit_PkgInit(int argc, char *argv[])
{
    int opt, r;
    pthread_t tid;

    host = NULL;
    cl_duplex_chan = NULL;

    timeout = default_timeout;

    while ((opt = getopt(argc, argv, "h:t:")) != -1) {
        switch (opt) {
        case 'h':
            host = optarg;
            break;
        case 't':
            timeout.tv_sec = atol(optarg);
            break;
        default:
            break;
        }
    }

    if (! host) {
        printf ("usage: %s -h server_host\n", argv[0]);
        return (EXIT_FAILURE);
    }

    cl_duplex_chan = clnt_create(host, FCHAN_PROG, FCHANV, "tcp");
    if (cl_duplex_chan == NULL) {
        clnt_pcreateerror(host);
        return (1);
    }

    /* start backchannel service using cl */
    r =  pthread_create(&tid, NULL, &backchannel_rpc_server,
                        (void*) cl_duplex_chan);

    thread_delay_s(1);

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

    return 0;
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite1(void)
{
    return 0;
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

	if (cl_stat != RPC_SUCCESS)
	    clnt_perror (cl_duplex_chan, "read_1m_1 seq ix failed");

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

    for (ix = 0; ix < 32; ++ix) {

        cl_stat = clnt_call(cl_duplex_chan, WRITE,
                            (xdrproc_t) xdr_write_args, (caddr_t) args,
                            (xdrproc_t) xdr_int, (caddr_t) &res,
                            timeout);

        CU_ASSERT_EQUAL(cl_stat, RPC_SUCCESS);

        /* update buffer */
        args->off += 32768;
        args->seqnum++;
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

    /* allocate -one- buffer for res */
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

	if (cl_stat != RPC_SUCCESS)
	    clnt_perror (cl_duplex_chan, "read_1m_1 seq ix failed");

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

#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "fchan.h"
#include "bchan.h"

#include "CUnit/Basic.h"


/*
 *  BEGIN SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

char *host;
CLIENT *cl_duplex_chan;
static struct timeval timeout, default_timeout = { 25, 0 };

static int
duplex_rpc_unit_PkgInit(int argc, char *argv[])
{
    int opt;
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

        /* TODO: maybe use results buffer */
        printf("read_1m_1: %s\n", res->data.data_val);
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

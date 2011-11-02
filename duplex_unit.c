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
        printf ("usage: %s server_host\n", argv[0]);
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

void overlapped_forward_calls_1(void)
{
    int code;
    enum clnt_stat cl_stat;

    cl_stat = clnt_call(cl_duplex_chan, BIND_CONN_TO_SESSION1,
                        (xdrproc_t) xdr_void, (caddr_t) NULL /* argp */,
                        (xdrproc_t) xdr_int, (caddr_t) &code,
                        timeout);

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
      { "Overlapped forward calls 1.", overlapped_forward_calls_1 },
      { "Some check.", check_1 },
      CU_TEST_INFO_NULL,
    };

    /* More Tests */


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

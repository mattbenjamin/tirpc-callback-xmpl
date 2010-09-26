
#include "bchan.h"

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

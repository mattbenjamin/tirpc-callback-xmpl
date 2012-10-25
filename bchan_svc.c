/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "bchan.h"
#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif

void
bchan_prog_1(struct svc_req *req, SVCXPRT *xprt)
{
    union {
        bchan_msg callback1_1_arg;
    } argument;
    union {
        bchan_res callback1_1_res;
    } result;
    bool_t retval;
    xdrproc_t _xdr_argument, _xdr_result;
    bool_t (*local)(char *, void *, struct svc_req *);

    switch (req->rq_proc) {
    case NULLPROC:
        (void) svc_sendreply(xprt, req, (xdrproc_t) xdr_void, NULL);
        return;

    case CALLBACK1:
        _xdr_argument = (xdrproc_t) xdr_bchan_msg;
        _xdr_result = (xdrproc_t) xdr_bchan_res;
        local = (bool_t (*) (char *, void *,  struct svc_req *))callback1_1_svc;
        break;

    default:
        svcerr_noproc(xprt, req);
        return;
    }
    memset ((char *)&argument, 0, sizeof (argument));
    if (!svc_getargs(xprt, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
        svcerr_decode(xprt, req);
        return;
    }
    retval = (bool_t) (*local)((char *)&argument, (void *)&result, req);
    if (retval > 0 && !svc_sendreply(xprt, req, (xdrproc_t) _xdr_result,
                                     &result)) {
        svcerr_systemerr(xprt, req);
    }
    if (!svc_freeargs(xprt, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
        fprintf (stderr, "%s", "unable to free arguments");
        exit (1);
    }
    if (!bchan_prog_1_freeresult(xprt, _xdr_result, (caddr_t) &result))
        fprintf (stderr, "%s", "unable to free results");

    return;
}


/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#ifndef _FCHAN_H_RPCGEN
#define _FCHAN_H_RPCGEN

#include <rpc/rpc.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif


struct fchan_msg {
	u_int seqnum;
	char *msg1;
	char *msg2;
};
typedef struct fchan_msg fchan_msg;

struct fchan_res {
	u_int result;
	char *msg1;
};
typedef struct fchan_res fchan_res;

#define FCHAN_PROG 0x20005001
#define FCHANV 1

#if defined(__STDC__) || defined(__cplusplus)
#define SENDMSG1 1
extern  enum clnt_stat sendmsg1_1(fchan_msg *, fchan_res *, CLIENT *);
extern  bool_t sendmsg1_1_svc(fchan_msg *, fchan_res *, struct svc_req *);
#define BIND_CONN_TO_SESSION1 2
extern  enum clnt_stat bind_conn_to_session1_1(void *, int *, CLIENT *);
extern  bool_t bind_conn_to_session1_1_svc(void *, int *, struct svc_req *);
extern int fchan_prog_1_freeresult (SVCXPRT *, xdrproc_t, caddr_t);

#else /* K&R C */
#define SENDMSG1 1
extern  enum clnt_stat sendmsg1_1();
extern  bool_t sendmsg1_1_svc();
#define BIND_CONN_TO_SESSION1 2
extern  enum clnt_stat bind_conn_to_session1_1();
extern  bool_t bind_conn_to_session1_1_svc();
extern int fchan_prog_1_freeresult ();
#endif /* K&R C */

/* the xdr functions */

#if defined(__STDC__) || defined(__cplusplus)
extern  bool_t xdr_fchan_msg (XDR *, fchan_msg*);
extern  bool_t xdr_fchan_res (XDR *, fchan_res*);

#else /* K&R C */
extern bool_t xdr_fchan_msg ();
extern bool_t xdr_fchan_res ();

#endif /* K&R C */

#ifdef __cplusplus
}
#endif

#endif /* !_FCHAN_H_RPCGEN */
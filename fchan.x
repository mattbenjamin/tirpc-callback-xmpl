
struct fchan_msg {
    unsigned int seqnum;
    string msg1<1024>;
    string msg2<1024>;
};

struct fchan_res {
    unsigned int result;
    string msg1<512>;
};

program FCHAN_PROG {
	version FCHANV {
            fchan_res SENDMSG1(fchan_msg) = 1;
	    int BIND_CONN_TO_SESSION1(void) = 2;
	} = 1;
} = 0x20005001;

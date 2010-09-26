
struct fchan_msg {
    unsigned int seqnum;
    string msg1<>;
    string msg2<>;
};

struct fchan_res {
    unsigned int result;
    string msg1<>;
};

program FCHAN_PROG {
	version FCHANV {
            fchan_res SENDMSG1(fchan_msg) = 1;
	} = 1;
} = 0x20005001;

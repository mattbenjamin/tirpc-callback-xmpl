
struct bchan_msg {
    unsigned int seqnum;
    string msg1<1024>;
    string msg2<1024>;
};

struct bchan_res {
    unsigned int result;
    string msg1<512>;
};

program BCHAN_PROG {
	version BCHANV {
            bchan_res CALLBACK1(bchan_msg) = 1;
	} = 1;	      
} = 0x20005002;

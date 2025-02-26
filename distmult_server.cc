#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <poll.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <vector>
#include <sys/resource.h>

#include <xdp/libxdp.h>
#include <bpf/bpf.h>
#include <xdp/xsk.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include "distmult_service.pb.h"
#include "task_handler.h"
#include "matrix_handler.h"

extern "C"
{
#include "../common/common_params.h"
#include "../common/common_user_bpf_xdp.h"
#include "../common/common_libbpf.h"
}
using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;

std::atomic<bool> running(true); // Flag to control the server loop

#define RX_QUEUE_SIZE 4096
#define FRAME_SIZE 2048
#define NUM_FRAMES 4096
#define INVALID_UMEM_FRAME UINT64_MAX
#define RX_BATCH_SIZE      64
static struct xdp_program *prog;
int xsk_map_fd;
bool custom_xsk = false;
struct config cfg = {
	.ifindex   = -1,
};

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};
struct stats_record {
	uint64_t timestamp;
	uint64_t rx_packets;
	uint64_t rx_bytes;
	uint64_t tx_packets;
	uint64_t tx_bytes;
};
struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;

	uint64_t umem_frame_addr[NUM_FRAMES];
	uint32_t umem_frame_free;

	uint32_t outstanding_tx;

	struct stats_record stats;
	struct stats_record prev_stats;
};

static inline __u32 xsk_ring_prod__free(struct xsk_ring_prod *r)
{
	r->cached_cons = *r->consumer + r->size;
	return r->cached_cons - r->cached_prod;
}
static const char *__doc__ = "AF_XDP kernel bypass example\n";
/**extern "C"{
static const struct option_wrapper long_options[] = {

	{{"help",	 no_argument,		NULL, 'h' },
	 "Show help", false},

	{{"dev",	 required_argument,	NULL, 'd' },
	 "Operate on device <ifname>", "<ifname>", true},

	{{"skb-mode",	 no_argument,		NULL, 'S' },
	 "Install XDP program in SKB (AKA generic) mode"},

	{{"native-mode", no_argument,		NULL, 'N' },
	 "Install XDP program in native mode"},

	{{"auto-mode",	 no_argument,		NULL, 'A' },
	 "Auto-detect SKB or native mode"},

	{{"force",	 no_argument,		NULL, 'F' },
	 "Force install, replacing existing program on interface"},

	{{"copy",        no_argument,		NULL, 'c' },
	 "Force copy mode"},

	{{"zero-copy",	 no_argument,		NULL, 'z' },
	 "Force zero-copy mode"},

	{{"queue",	 required_argument,	NULL, 'Q' },
	 "Configure interface receive queue for AF_XDP, default=0"},

	{{"poll-mode",	 no_argument,		NULL, 'p' },
	 "Use the poll() API waiting for packets to arrive"},

	{{"quiet",	 no_argument,		NULL, 'q' },
	 "Quiet mode (no output)"},

	{{"filename",    required_argument,	NULL,  1  },
	 "Load program from <file>", "<file>"},

	{{"progname",	 required_argument,	NULL,  2  },
	 "Load program from function <name> in the ELF file", "<name>"},

	{{0, 0, NULL,  0 }, NULL, false}
};
}**/
static bool global_exit;

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
	struct xsk_umem_info *umem = new xsk_umem_info{};;
	int ret;

	//umem = calloc(1, sizeof(*umem));
	//auto umem = std::make_unique<xsk_umem_info>();
	if (!umem)
		return NULL;

	ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq,
			       NULL);
	if (ret) {
		errno = -ret;
		return NULL;
	}

	umem->buffer = buffer;
	return umem;
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
	uint64_t frame;
	if (xsk->umem_frame_free == 0)
		return INVALID_UMEM_FRAME;

	frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
	xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
	return frame;
}
static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
	assert(xsk->umem_frame_free < NUM_FRAMES);

	xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
}

static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk)
{
	return xsk->umem_frame_free;
}
static struct xsk_socket_info *xsk_configure_socket(struct config *cfg, struct xsk_umem_info *umem)
{
	struct xsk_socket_config xsk_cfg;
	struct xsk_socket_info *xsk_info = new xsk_socket_info{};;
	uint32_t idx;
	int i;
	int ret;
	uint32_t prog_id;

	//xsk_info = calloc(1, sizeof(*xsk_info));
	//auto xsk_info = std::make_unique<xsk_socket_info>();
	if (!xsk_info)
		return NULL;

	xsk_info->umem = umem;
	xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	xsk_cfg.xdp_flags = cfg->xdp_flags;
	xsk_cfg.bind_flags = cfg->xsk_bind_flags;
	xsk_cfg.libbpf_flags = (custom_xsk) ? XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD: 0;
	ret = xsk_socket__create(&xsk_info->xsk, cfg->ifname,
				 cfg->xsk_if_queue, umem->umem, &xsk_info->rx,
				 &xsk_info->tx, &xsk_cfg);
	if (ret)
		goto error_exit;

	if (custom_xsk) {
		ret = xsk_socket__update_xskmap(xsk_info->xsk, xsk_map_fd);
		if (ret)
			goto error_exit;
	} else {
		/* Getting the program ID must be after the xdp_socket__create() call */
		if (bpf_xdp_query_id(cfg->ifindex, cfg->xdp_flags, &prog_id))
			goto error_exit;
	}

	/* Initialize umem frame allocation */
	for (i = 0; i < NUM_FRAMES; i++)
		xsk_info->umem_frame_addr[i] = i * FRAME_SIZE;

	xsk_info->umem_frame_free = NUM_FRAMES;

	/* Stuff the receive /ath with buffers, we assume we have enough */
	ret = xsk_ring_prod__reserve(&xsk_info->umem->fq,
				     XSK_RING_PROD__DEFAULT_NUM_DESCS,
				     &idx);

	if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
		goto error_exit;

	for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i ++)
		*xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) =
			xsk_alloc_umem_frame(xsk_info);

	xsk_ring_prod__submit(&xsk_info->umem->fq,
			      XSK_RING_PROD__DEFAULT_NUM_DESCS);

	return xsk_info;

error_exit:
	errno = -ret;
	return NULL;
}

static void complete_tx(struct xsk_socket_info *xsk)
{
	unsigned int completed;
	uint32_t idx_cq;

	if (!xsk->outstanding_tx)
		return;

	sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

	/* Collect/free completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk->umem->cq,
					XSK_RING_CONS__DEFAULT_NUM_DESCS,
					&idx_cq);

	if (completed > 0) {
		for (int i = 0; i < completed; i++)
			xsk_free_umem_frame(xsk,
					    *xsk_ring_cons__comp_addr(&xsk->umem->cq,
								      idx_cq++));

		xsk_ring_cons__release(&xsk->umem->cq, completed);
		xsk->outstanding_tx -= completed < xsk->outstanding_tx ?
			completed : xsk->outstanding_tx;
	}
}
extern "C"{
static inline __sum16 csum16_add(__sum16 csum, __be16 addend)
{
	uint16_t res = (uint16_t)csum;

	res += (__u16)addend;
	return (__sum16)(res + (res < (__u16)addend));
}

static inline __sum16 csum16_sub(__sum16 csum, __be16 addend)
{
	return csum16_add(csum, ~addend);
}

static inline void csum_replace2(__sum16 *sum, __be16 old, __be16 new_val)
{
	*sum = ~csum16_add(csum16_sub(~(*sum), old), new_val);
}
}
static bool process_packet(struct xsk_socket_info *xsk,
			   uint64_t addr, uint32_t len)
{
	uint8_t *pkt = (uint8_t*)xsk_umem__get_data(xsk->umem->buffer, addr);

	/* Lesson#3: Write an IPv6 ICMP ECHO parser to send responses
	 *
	 * Some assumptions to make it easier:
	 * - No VLAN handling
	 * - Only if nexthdr is ICMP
	 * - Just return all data with MAC/IP swapped, and type set to
	 *   ICMPV6_ECHO_REPLY
	 * - Recalculate the icmp checksum */

	if (false) {
		int ret;
		uint32_t tx_idx = 0;
		uint8_t tmp_mac[ETH_ALEN];
		struct in6_addr tmp_ip;
		struct ethhdr *eth = (struct ethhdr *) pkt;
		struct ipv6hdr *ipv6 = (struct ipv6hdr *) (eth + 1);
		struct icmp6hdr *icmp = (struct icmp6hdr *) (ipv6 + 1);

		if (ntohs(eth->h_proto) != ETH_P_IPV6 ||
		    len < (sizeof(*eth) + sizeof(*ipv6) + sizeof(*icmp)) ||
		    ipv6->nexthdr != IPPROTO_ICMPV6 ||
		    icmp->icmp6_type != ICMPV6_ECHO_REQUEST)
			return false;

		memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
		memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
		memcpy(eth->h_source, tmp_mac, ETH_ALEN);

		memcpy(&tmp_ip, &ipv6->saddr, sizeof(tmp_ip));
		memcpy(&ipv6->saddr, &ipv6->daddr, sizeof(tmp_ip));
		memcpy(&ipv6->daddr, &tmp_ip, sizeof(tmp_ip));

		icmp->icmp6_type = ICMPV6_ECHO_REPLY;

		csum_replace2(&icmp->icmp6_cksum,
			      htons(ICMPV6_ECHO_REQUEST << 8),
			      htons(ICMPV6_ECHO_REPLY << 8));

		/* Here we sent the packet out of the receive port. Note that
		 * we allocate one entry and schedule it. Your design would be
		 * faster if you do batch processing/transmission */

		ret = xsk_ring_prod__reserve(&xsk->tx, 1, &tx_idx);
		if (ret != 1) {
			/* No more transmit slots, drop the packet */
			return false;
		}

		xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->len = len;
		xsk_ring_prod__submit(&xsk->tx, 1);
		xsk->outstanding_tx++;

		xsk->stats.tx_bytes += len;
		xsk->stats.tx_packets++;
		return true;
	}

	return false;
}

static void handle_receive_packets(struct xsk_socket_info *xsk)
{
	unsigned int rcvd, stock_frames, i;
	uint32_t idx_rx = 0, idx_fq = 0;
	int ret;

	rcvd = xsk_ring_cons__peek(&xsk->rx, RX_BATCH_SIZE, &idx_rx);
	if (!rcvd)
		return;

	/* Stuff the ring with as much frames as possible */
	stock_frames = xsk_prod_nb_free(&xsk->umem->fq,
					xsk_umem_free_frames(xsk));

	if (stock_frames > 0) {

		ret = xsk_ring_prod__reserve(&xsk->umem->fq, stock_frames,
					     &idx_fq);

		/* This should not happen, but just in case */
		while (ret != stock_frames)
			ret = xsk_ring_prod__reserve(&xsk->umem->fq, rcvd,
						     &idx_fq);

		for (i = 0; i < stock_frames; i++)
			*xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) =
				xsk_alloc_umem_frame(xsk);

		xsk_ring_prod__submit(&xsk->umem->fq, stock_frames);
	}

	/* Process received packets */
	for (i = 0; i < rcvd; i++) {
		uint64_t addr = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx)->addr;
		uint32_t len = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++)->len;

		if (!process_packet(xsk, addr, len))
			xsk_free_umem_frame(xsk, addr);

		xsk->stats.rx_bytes += len;
	}

	xsk_ring_cons__release(&xsk->rx, rcvd);
	xsk->stats.rx_packets += rcvd;

	/* Do we need to wake up the kernel for transmission */
	complete_tx(xsk);
  }

static void rx_and_process(struct config *cfg,
			   struct xsk_socket_info *xsk_socket)
{
	struct pollfd fds[2];
	int ret, nfds = 1;

	memset(fds, 0, sizeof(fds));
	fds[0].fd = xsk_socket__fd(xsk_socket->xsk);
	fds[0].events = POLLIN;

	while(!global_exit) {
		if (cfg->xsk_poll_mode) {
			ret = poll(fds, nfds, -1);
			if (ret <= 0 || ret > 1)
				continue;
		}
		handle_receive_packets(xsk_socket);
	}
}

class DistMultServer
{
public:
    explicit DistMultServer(const std::string &ip_address, int port, TaskHandler *handler)
        : task_handler_(handler), stop_reader_thread_(false), stop_writer_thread_(false)
    {
	
	int ret;
        const char *ifname = "eth0";   // change to your network interface
        int queue_id = 0;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts, 0);
	struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
	uint64_t packet_buffer_size = NUM_FRAMES * FRAME_SIZE;
	int err;
	char errmsg[1024];
	 config cfg = {
		.attach_mode = XDP_MODE_UNSPEC,/**XDP_MODE_HW**/
		.ifindex = '0',
		.ifname = (char *)cfg.ifname_buf,
		.ifname_buf = "eth0",
		.reuse_maps = true,
		.filename = "af_xdp_kern.o",
	        //.xsk_if_queue = 0,	
		//.do_unload = true, 
		//.prog_id = 0, 
		.progname = "xdp_prog_filter",
		//.xsk_poll_mode = true, 
		.src_mac = 0,
		.dest_mac = 0
		//.xsk_bind_flags = cfg.xsk_bind_flags &= XDP_ZEROCOPY,
		
		//add in name and all the rest here
	};

	//signal(SIGNIT, exit_application);

	//parse_cmdline_args(argc, argv, long_options, &cfg, __doc__);
	
	/* Required option */
	//if (cfg.ifindex == -1){
	//	std::cerr << "ERROR: Required option --dev missing" << std::endl;
	//	usage(argv[0], __doc__, long options, (argc == 1));
	//	return;
	//}
	err = do_unload(&cfg);
	if (err) {
		libxdp_strerror(err, errmsg, sizeof(errmsg));
		std::cerr << "Couldn't unload XDP program " << cfg.progname << ": " <<  errmsg;
		return;
	}

	std::cout << "Success: Unloading XDP prog name: " << cfg.progname << std::endl;
	/*Load custion program if configured*/
	if (cfg.filename[0] != 0){
		struct bpf_map *map;

		custom_xsk = true;
		xdp_opts.open_filename = cfg.filename;
		xdp_opts.prog_name = cfg.progname;
		xdp_opts.opts = &opts;

		if (cfg.progname[0] != 0) {
			xdp_opts.open_filename = cfg.filename;
			xdp_opts.prog_name = cfg.progname;
			xdp_opts.opts = &opts;

			prog = xdp_program__create(&xdp_opts);
		} else {
			prog = xdp_program__open_file(cfg.filename,
						  NULL, &opts);
		}
		err = libxdp_get_error(prog);
		if (err) {
			libxdp_strerror(err, errmsg, sizeof(errmsg));
			LOG(INFO) <<  "ERR: loading program: " <<  errmsg;
			return;
		}

		err = xdp_program__attach(prog, cfg.ifindex, cfg.attach_mode, 0);
		if (err) {
			libxdp_strerror(err, errmsg, sizeof(errmsg));
			LOG(INFO) << "Couldn't attach XDP program on iface " << cfg.ifname << ": " << errmsg << ": " << err;
			return;
		}

		/* We also need to load the xsks_map */
		map = bpf_object__find_map_by_name(xdp_program__bpf_obj(prog), "xsks_map");
		xsk_map_fd = bpf_map__fd(map);
		if (xsk_map_fd < 0) {
			LOG(INFO) << "ERROR: no xsks map found: " << strerror(xsk_map_fd);
			//exit(EXIT_FAILURE);
			return;
		}

	}

	if (posix_memalign(&packet_buffer, 
				getpagesize(),
				packet_buffer_size)){
		std::cerr << "ERROR: Can't alloccate buffer memory" <<std::endl;
		return;
	
	}
	//memset(packet_buffer, 0, packet_buffer_size);
	
	umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
	if (umem == NULL){
		std::cerr << "Can't create umem " << std::endl;
		return;
	}

	/* Create and bind the AF_XDP socket.
	 * This call associates the UMEM with a specific network interface and queue.
         * Under the hood, it maps the necessary rings so that the application can
         * access descriptors directly from user space. */
	xsk_socket = xsk_configure_socket(&cfg, umem);
	if (xsk_socket == NULL){
		std::cerr << "Failed to create AF_XDP socket. " << std::endl;
		//xdp_umem_unreg(umem);
		free(packet_buffer);
	        return;	
	}	
	std::cout << "AF_XDP server is ready to run on " << ifname << "(queue " << queue_id << ")" << std::endl;

        handler->initialize_buffers();
        if (!stop_reader_thread_)
        {
            std::cout << "Start reader and writer threads" << std::endl;
            //reader_thread_ = std::thread(&DistMultServer::start_reading, this);
            //writer_thread_ = std::thread(&DistMultServer::writer_thread, this);
        
	    //pin_thread_to_core(reader_thread_, 0);
	    //pin_thread_to_core(writer_thread_, 0);
	}
    }

    ~DistMultServer()
    {
        stop();
    }

private:
    bool stop_writer_thread_;
    bool stop_reader_thread_;
    std::thread reader_thread_;
    std::thread writer_thread_;
    TaskHandler *task_handler_;
    std::mutex task_lock_;
    int current_buffer_;
    const char *ifname = "eth0";   // change to your network interface
    int queue_id = 0;
    void *packet_buffer;
    //struct xdp_umem *umem = NULL;
    //struct xdp_socket *xsk = NULL;

    //struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    struct xsk_umem_info *umem;
    struct xsk_socket_info *xsk_socket;
     
    void pin_thread_to_core(std::thread &thread, int core)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset); // Pin to core 0

        int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
        {
            std::cerr << "Error setting thread affinity: " << strerror(rc) << std::endl;
        }
    }

    void start_reading(){
    /* Main packet processing loop.
       Here we poll for packets on the RX ring. For each packet, we retrieve
       its address and length from the descriptor, then obtain a pointer to
       the actual packet data in UMEM.
       
       Note: xdp_socket_rx_burst() and xdp_socket_release_rx() are libxdp helper
       functions that abstract away some of the ring details. */	
	while(!stop_writer_thread_){
		rx_and_process(&cfg, xsk_socket);   
	/** 
	    memset(fds, 0, sizeof(fds));
	    fds[0].fd = xsk_socket__fd(xsk_socket->xsk);
	    fds[0].events = POLLIN;

	    while(!global_exit) {
	    	if(cfg->xsk_poll_mode){
			ret = poll(fds, nfds, -1);
			if (ret <= 0 || ret > 1)
				continue;
		}
		handle_recieve_packets(xsk_socket);
	    }

	    //struct xdp_desc *rx_desc;
	    //unsigned int num_rx;

	    /*Poll for recieved packets. The function returns the number of available
           descriptors (i.e. packets) and sets rx_desc to point to the first descriptor. */
	    /**num_rx = xdp_socket__rx_burst(xsk, &rx_desc, NUM_FRAMES);
	    if (num_rx == 0){
	    	/*No packet recieved*/
		/**usleep(100); //change this to be better!
	    	continue;
	    }
	    
    	    /* Process each recieved packet */	    
	    /**char buffer[FRAME_SIZE]; 
	    current_buffer_ = task_handler_->select_next_buffer();
	    int buffer_id = current_buffer_;// / 4;
	    int thread_id = 0;//current_buffer_;// % 4;
	    std::cout << "current buffer is: " << current_buffer_ << std::endl; 
	    request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
            for (unsigned int i = 0; i < num_rx; i++){
	   	
		char *pkt = xdp__umem_get_data(umem, rx_desc[i].addr);
	        unsigned int pkt_len = rx_desc[i].len;
       	        std::cout << "Recieved packet of size " << pkt_len << "bytes" << std::endl;

                if (request_->ParseFromArray(pkt, pkt_len)) {
                    std::cout << "[INFO] Received MatrixRequest (task_id=" << request_->task_id() << ")" << std::endl;
                    task_handler_->process_request(buffer_id, 0);  // Simplified
                } else {
                    std::cerr << "[ERROR] Failed to parse request!" << std::endl;
                }

		xdp_socket__release(xsk, num_rx);
            }
        	/*Clean this up later*/
	}
		/* Cleanup */
	xsk_socket__delete(xsk_socket->xsk);
	xsk_umem__delete(umem->umem);
}
    
void writer_thread(){
	while (!stop_writer_thread_){
	
	    int response_id = task_handler_->check_response();
            if (response_id == -1)
            {
                LOG(INFO) << "OHNO Server writer: check response returns -1";
            }
            else {


                int buffer_id = response_id;// / 4;
                int thread_id = 0;// response_id % 4;
/**		
 		uint32_t idx;
                if (xsk_ring_prod__reserve(&xsk_->tx, 1, &idx)) {
                	struct xdp_desc *desc = xsk_ring_prod__tx_desc(&xsk_->tx, idx);
                	char *pkt_data = (char *)xsk_umem__get_data(xsk_->umem->buffer, desc->addr);

                	response_ = (MatrixResponse *)task_handler_->get_buffer_response(buffer_id, 0);
                	std::string serialized_response;
                	response_->SerializeToString(&serialized_response);

                	memcpy(pkt_data, serialized_response.c_str(), serialized_response.size());
                	desc->len = serialized_response.size();

                	xsk_ring_prod__submit(&xsk_->tx, 1);
            	}
		std::cout << "buffer_id: " << buffer_id << " thread_id: " << thread_id << std::endl; 
		}
	**/	
	    }
       }
}

    void stop()
    {
      
        if (reader_thread_.joinable())
        {
            reader_thread_.join();
        }
        if (writer_thread_.joinable())
        {
            writer_thread_.join();
        }
	running = false;
    }

    MatrixRequest *request_;
    MatrixResponse *response_;
};

void RunServer(const std::string &task_type, uint32_t task_size, const std::string &address)
{

    std::string server_address(address);
    std::unique_ptr<TaskHandler> handler;

    if (task_type == "matrix")
    {
        handler = std::make_unique<MatrixClass>(task_size);
    }
    else
    {
        std::cerr << "Unsupported task type: " << task_type << std::endl;
        return;
    }

    // Use unique_ptr to manage server lifetime
    auto server = std::make_unique<DistMultServer>(server_address, 5001, handler.get());
   
   
    while (running)
    {
        
	   std::this_thread::sleep_for(std::chrono::seconds(1));//here I think the issue is....
    }
}
int main(int argc, char **argv)
{
    absl::ParseCommandLine(argc, argv);
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::InitializeLog();

    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " <task_type> <n> <<address>>\n";
        return EXIT_FAILURE;
    }

    std::string task_type = argv[1];
    std::string address = argv[3];
    int n = atoi(argv[2]);

    if (task_type == "matrix")
    {
        try
        {
            RunServer(task_type, n, address);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error running server: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
    else
    {
        std::cerr << "Error: Unsupported task type \"" << task_type << "\". Supported: \"matrix\".\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

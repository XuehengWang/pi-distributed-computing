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
#include <net/ethernet.h>   // Ethernet header definitions.
#include <netinet/ip.h>     // IPv4 header definitions.
#include <netinet/udp.h>    // UDP header definitions.
#include <linux/if_link.h>  // Sometimes needed for certain definitions.
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
#define FRAME_SIZE 8192
#define NUM_FRAMES 4096
#define INVALID_UMEM_FRAME UINT64_MAX
#define RX_BATCH_SIZE      64
#define HEADER_SIZE 	
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

// Helper function: prints a hex dump of data.
void print_hex(const uint8_t* data, size_t len) {
    std::cout << "Hex dump:" << std::endl;
    for (size_t i = 0; i < len; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 16 == 0)
            std::cout << std::endl;
    }
    std::cout << std::dec << std::endl;
}

// Helper function: converts a MAC address to a string.
std::string mac_to_string(const uint8_t mac[ETH_ALEN]) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < ETH_ALEN; i++) {
        oss << std::setw(2) << static_cast<int>(mac[i]);
        if (i < ETH_ALEN - 1)
            oss << ":";
    }
    return oss.str();
}
static inline __u32 xsk_ring_prod__free(struct xsk_ring_prod *r)
{
	r->cached_cons = *r->consumer + r->size;
	return r->cached_cons - r->cached_prod;
}
static const char *__doc__ = "AF_XDP kernel bypass example\n";
extern "C"{
static const struct option_wrapper long_options[] = {

	{{"help",	 no_argument,		NULL, 'h' },
	 "Show help", NULL, false},

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

	{{0, 0, NULL,  0 }, NULL, NULL, false}
};
}
static bool global_exit;
// Use a constexpr for the constant.
constexpr uint64_t NANOSEC_PER_SEC = 1000000000ULL;
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
// Helper: compute the IP header checksum.
uint16_t ip_checksum(void* vdata, size_t length) {
    uint32_t sum = 0;
    uint8_t* data = reinterpret_cast<uint8_t*>(vdata);
    for (size_t i = 0; i < length; i += 2) {
        uint16_t word = (data[i] << 8);
        if (i + 1 < length)
            word |= data[i+1];
        sum += word;
    }
    // Fold 32-bit sum to 16 bits.
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

uint16_t udp_checksum(const struct iphdr* ip, const struct udphdr* udp, const uint8_t* payload, size_t payload_len) {
    uint32_t sum = 0;
    
    // Pseudo-header fields: source IP, destination IP, protocol, UDP length.
    sum += (ip->saddr >> 16) & 0xFFFF;
    sum += ip->saddr & 0xFFFF;
    sum += (ip->daddr >> 16) & 0xFFFF;
    sum += ip->daddr & 0xFFFF;
    sum += IPPROTO_UDP;//ip->protocol); // protocol is 17 (UDP) in network order.
    // Add the pseudo-header's protocol and UDP length.
    // The pseudo-header consists of a zero byte and the protocol.
    // Instead of using htons(ip->protocol) (which on a little-endian machine yields 0x1100 for protocol 17),
    // add the protocol directly (0x0011 for UDP).
    //sum += static_cast<uint16_t>(ip->protocol);
    sum += udp->len;
    // Sum UDP header and payload.
    // Create a pointer that starts at the UDP header.
    const uint8_t* udp_ptr = reinterpret_cast<const uint8_t*>(udp);

    // Total length in bytes (convert from network order).
    size_t udp_total_len = ntohs(udp->len);
    // Sum over the UDP header and payload.
    for (size_t i = 0; i < sizeof(struct udphdr); i += 2) {
        uint16_t word = udp_ptr[i] << 8;
        if (i + 1 < sizeof(struct udphdr))
            word |= udp_ptr[i+1];
        else
            word |= 0;  // pad with zero if odd number of bytes.
        sum += word;
    }
    
    // Sum the payload.
    const uint8_t* payload_ptr = payload;
    for (size_t i = 0; i < payload_len; i += 2) {
        uint16_t word = payload_ptr[i] << 8;
        if (i + 1 < payload_len)
            word |= payload_ptr[i + 1];
        else
            word |= 0; // pad if odd length.
        sum += word;
    }

    // Fold 32-bit sum to 16 bits.
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    
    uint16_t checksum = static_cast<uint16_t>(~sum);
    // Per RFC, a checksum result of 0 should be transmitted as 0xFFFF.
    return checksum ? checksum : 0xFFFF;
}
class DistMultServer
{
public:
    explicit DistMultServer(const std::string &ip_address, int port, TaskHandler *handler, config *cfg)
        : task_handler_(handler), stop_reader_thread_(false), stop_writer_thread_(false), cfg_(cfg)
    {

	int ret;
	void *packet_buffer;
	uint64_t packet_buffer_size;
	struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
	int err;
	char errmsg[1024];

	//PASS THROUGH CONFIG FILE
	/* Allow unlimited locking of memory, so all memory needed for packet
	 * buffers can be locked.
	 *
	 * NOTE: since kernel v5.11, eBPF maps allocations are not tracked
	 * through the process anymore. Now, eBPF maps are accounted to the
	 * current cgroup of which the process that created the map is part of
	 * (assuming the kernel was built with CONFIG_MEMCG).
	 *
	 * Therefore, you should ensure an appropriate memory.max setting on
	 * the cgroup (via sysfs, for example) instead of relying on rlimit.
	 */
	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
		fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	/* Allocate memory for NUM_FRAMES of the default XDP frame size */
	packet_buffer_size = NUM_FRAMES * FRAME_SIZE;	
	if (posix_memalign(&packet_buffer, 
				getpagesize(),
				packet_buffer_size)){
		std::cerr << "ERROR: Can't alloccate buffer memory" <<std::endl;
		return;
	
	}
	//memset(packet_buffer, 0, packet_buffer_size);
	
	umem_ = configure_xsk_umem(packet_buffer, packet_buffer_size);
	if (umem_ == NULL){
		std::cerr << "Can't create umem " << std::endl;
		return;
	}
	
	/* Create and bind the AF_XDP socket.
	 * This call associates the UMEM with a specific network interface and queue.
         * Under the hood, it maps the necessary rings so that the application can
         * access descriptors directly from user space. */
	xsk_configure_socket();
	if (xsk_ == NULL){
		std::cerr << "Failed to create AF_XDP socket. " << std::endl;
		//xdp_umem_unreg(umem);
		free(packet_buffer);
	        return;	
	}	
	std::cout << "AF_XDP server is ready to run on " << cfg_->ifname << "(queue " << cfg->ifindex << ")" << std::endl;
	
	preallocate_headers(ip_address, port);	
        
	handler->initialize_buffers();
        
	if (!stop_reader_thread_)
        {
            reader_thread_ = std::thread(&DistMultServer::start_reading, this);
            writer_thread_ = std::thread(&DistMultServer::writer_thread, this);
            stats_poll_thread = std::thread(&DistMultServer::stats_poll, this);
	    pin_thread_to_core(reader_thread_, 0);
	    pin_thread_to_core(writer_thread_, 0);
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
    struct config *cfg_;
    //struct xsk_umem_info umem_;
    //struct xsk_socket_info xsk_;
    // Dynamically allocate the structs.
    xsk_umem_info* umem_ = new xsk_umem_info();
    xsk_socket_info* xsk_ = new xsk_socket_info();
    std::thread stats_poll_thread;
    std::vector<uint8_t> header_template_;
    size_t header_size_;
	
    	//Get the current time in nanoseconds using CLOCK_MONOTONIC.
	uint64_t gettime() {
	    timespec t;
	    int res = clock_gettime(CLOCK_MONOTONIC, &t);
	    if (res < 0) {
		std::fprintf(stderr, "Error with clock_gettime! (%i)\n", res);
		std::exit(EXIT_FAILURE);
	    }
	    return static_cast<uint64_t>(t.tv_sec) * NANOSEC_PER_SEC + t.tv_nsec;
	}

	// Calculate the time period (in seconds) between two stats records.
	double calc_period(const stats_record &current, const stats_record &previous) {
	    uint64_t period = current.timestamp - previous.timestamp;
	    return (period > 0) ? static_cast<double>(period) / NANOSEC_PER_SEC : 0.0;
	}

	// Print statistics for RX and TX.
	void stats_print(const stats_record &stats_rec, const stats_record &stats_prev) {
	    uint64_t packets, bytes;
	    double period = calc_period(stats_rec, stats_prev);
	    if (period == 0)
		period = 1; // Avoid division by zero.
	    double pps; // packets per second
	    double bps; // bits per second

	    // Format string with thousands separators using %'
	    const char *fmt = "%-12s %'11lld pkts (%'10.0f pps) %'11lld Kbytes (%'6.0f Mbits/s) period:%f\n";

	    // Calculate and print RX stats.
	    packets = stats_rec.rx_packets - stats_prev.rx_packets;
	    pps = packets / period;
	    bytes = stats_rec.rx_bytes - stats_prev.rx_bytes;
	    bps = (bytes * 8) / period / 1000000;
	    std::printf(fmt, "AF_XDP RX:", stats_rec.rx_packets, pps,
			stats_rec.rx_bytes / 1000, bps, period);

	    // Calculate and print TX stats.
	    packets = stats_rec.tx_packets - stats_prev.tx_packets;
	    pps = packets / period;
	    bytes = stats_rec.tx_bytes - stats_prev.tx_bytes;
	    bps = (bytes * 8) / period / 1000000;
	    std::printf(fmt, "       TX:", stats_rec.tx_packets, pps,
			stats_rec.tx_bytes / 1000, bps, period);

	    std::printf("\n");
	}

	// Thread function to poll and print statistics periodically.
	void stats_poll() {
	    const unsigned int interval = 2;
	    static stats_record previous_stats = {0};

	    previous_stats.timestamp = gettime();

	    // Set the locale for pretty printing with thousands separators.
	    setlocale(LC_NUMERIC, "en_US");

	    while (!global_exit) {
		// Sleep using C++ standard library (you could also use std::this_thread::sleep_for).
		std::this_thread::sleep_for(std::chrono::seconds(interval));
		xsk_->stats.timestamp = gettime();
		stats_print(xsk_->stats, previous_stats);
		previous_stats = xsk_->stats;
	    }
	}
	 
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

	// Helper function to allocate a UMEM frame (simplified; you should have proper free-list management)
	uint64_t allocate_umem_frame() {
	    static uint64_t next_frame = 0;
	    uint64_t addr = next_frame;
	    next_frame += FRAME_SIZE;
	    return addr;
	}

	
	struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
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

	uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
	{
		uint64_t frame;
		if (xsk_->umem_frame_free == 0)
			return INVALID_UMEM_FRAME;

		frame = xsk_->umem_frame_addr[--xsk_->umem_frame_free];
		xsk_->umem_frame_addr[xsk_->umem_frame_free] = INVALID_UMEM_FRAME;
		return frame;
	}

	void xsk_free_umem_frame(uint64_t frame)
	{
		assert(xsk_->umem_frame_free < NUM_FRAMES);

		xsk_->umem_frame_addr[xsk_->umem_frame_free++] = frame;
	}

	uint64_t xsk_umem_free_frames()
	{
		return xsk_->umem_frame_free;
	}

	void xsk_configure_socket()
	{
		struct xsk_socket_config xsk_cfg;
		//struct xsk_socket_info *xsk_info = new xsk_socket_info{};
		uint32_t idx;
		int i;
		int ret;
		uint32_t prog_id;

		//xsk_info = calloc(1, sizeof(*xsk_info));
		//auto xsk_info = std::make_unique<xsk_socket_info>();
		if (!xsk_)
			return;

		xsk_->umem = umem_;
		xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
		xsk_cfg.xdp_flags = cfg_->xdp_flags;
		xsk_cfg.bind_flags = cfg_->xsk_bind_flags;
		xsk_cfg.libbpf_flags = (custom_xsk) ? XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD: 0;
		ret = xsk_socket__create(&xsk_->xsk, cfg_->ifname,
					 cfg_->xsk_if_queue, umem_->umem, &xsk_->rx,
					 &xsk_->tx, &xsk_cfg);
		if (ret)
			goto error_exit;

		if (custom_xsk) {
			ret = xsk_socket__update_xskmap(xsk_->xsk, xsk_map_fd);
			if (ret)
				goto error_exit;
		} else {
			/* Getting the program ID must be after the xdp_socket__create() call */
			if (bpf_xdp_query_id(cfg_->ifindex, cfg_->xdp_flags, &prog_id))
				goto error_exit;
		}

		/* Initialize umem frame allocation */
		for (i = 0; i < NUM_FRAMES; i++)
			xsk_->umem_frame_addr[i] = i * FRAME_SIZE;

		xsk_->umem_frame_free = NUM_FRAMES;

		/* Stuff the receive /ath with buffers, we assume we have enough */
		ret = xsk_ring_prod__reserve(&xsk_->umem->fq,
					     XSK_RING_PROD__DEFAULT_NUM_DESCS,
					     &idx);

		if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
			goto error_exit;

		for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i ++)
			*xsk_ring_prod__fill_addr(&xsk_->umem->fq, idx++) =
				xsk_alloc_umem_frame(xsk_);

		xsk_ring_prod__submit(&xsk_->umem->fq,
				      XSK_RING_PROD__DEFAULT_NUM_DESCS);

		//return xsk_;

	error_exit:
		errno = -ret;
		//return NULL;
	}
	
	void preallocate_headers(const std::string &ip_address, int port){

    	    // Configure your own (source) addresses (these must be known or determined by your system)
    	    uint8_t src_mac[ETH_ALEN] = {0x2c, 0xcf, 0x67, 0x13, 0xa7, 0x40}; // Example source MAC
									      // 2c:cf:67:13:a7:40
    	    const char* src_ip_str = "192.168.1.129";
    	    uint16_t src_udp_port = 40328; // Example source UDP port

	    uint8_t dest_mac[ETH_ALEN] = {0x28, 0xc5, 0xc8, 0xb8, 0x38, 0x9b}; // Replace with the actual destination MAC. 
									       // 28:c5:c8:b8:38:9b
            // Convert IP strings to binary format.
    	    struct in_addr src_ip, dest_ip;
    	    if (inet_aton(src_ip_str, &src_ip) == 0) {
        	std::cerr << "Invalid source IP address" << std::endl;
//        	return false;
    	    }
    	    
	    if (inet_aton(ip_address.c_str(), &dest_ip) == 0) {
        	std::cerr << "Invalid destination IP address" << std::endl;
  //      	return false;
    	    }
	    header_size_ = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	    // Fill Ethernet header.
	    
	    header_template_ = std::vector<uint8_t>(header_size_);
	    //
	    struct ethhdr *eth = reinterpret_cast<struct ethhdr*>(header_template_.data());
	    memcpy(eth->h_source, src_mac, ETH_ALEN);
	    memcpy(eth->h_dest, dest_mac, ETH_ALEN);
	    eth->h_proto = htons(ETH_P_IP);
		
	    // Fill IP header.
	    struct iphdr *ip = reinterpret_cast<struct iphdr*>(header_template_.data() + sizeof(struct ethhdr));
	    ip->version = 4;
	    ip->ihl = 5;
	    ip->tos = 0;
	    ip->tot_len = 0;  // To be filled per-packet.
	    ip->id = htons(0);
	    ip->frag_off = 0;
	    ip->ttl = 64;
	    ip->protocol = IPPROTO_UDP;
	    ip->saddr = src_ip.s_addr;
	    ip->daddr = dest_ip.s_addr;
	    // ip->check will be recalculated.

	    // Fill UDP header.
	    struct udphdr *udp = reinterpret_cast<struct udphdr*>(header_template_.data() + sizeof(struct ethhdr) + sizeof(struct iphdr));
	    udp->source = htons(src_udp_port);
	    udp->dest = htons(port);
	    udp->len = 0;   // To be filled per-packet.
	    udp->check = 0; // Can be computed if needed.
	
	}

	// Function to send a response message via the AF_XDP TX ring.
	bool send_response(const char* response, size_t resp_len) {
	    
	    uint32_t tx_idx;
	    // Reserve one descriptor from the TX ring.
	    int ret = xsk_ring_prod__reserve(&xsk_->tx, 1, &tx_idx);
	    if (ret != 1) {
		std::cerr << "Failed to reserve TX descriptor: " << strerror(errno) << std::endl;
		return false;
	    }

	    // Allocate a UMEM frame for the outgoing packet.
	    uint64_t frame_addr = allocate_umem_frame();
	    // Get the pointer to the UMEM data.
	    void* frame_data = xsk_umem__get_data(umem_->buffer, frame_addr);
	    if (!frame_data) {
		std::cerr << "Failed to get UMEM data pointer" << std::endl;
		// Discard the reserved descriptor.
		//xsk_ring_prod__discard(&xsk_->tx, 1);
		return false;
	    }

	    memset(frame_data, 0, FRAME_SIZE);
    	    
	    // Copy the pre-built header template into the frame.
    	    memcpy(frame_data, header_template_.data(), header_size_);

    	    // Append the payload immediately after the header.
    	    memcpy(reinterpret_cast<uint8_t*>(frame_data) + header_size_, response, resp_len);
	    std::cout << header_template_.data() << ": " << header_size_ << std::endl;
	    
	    // IP total length = header of IP (20 bytes) + UDP header (8 bytes) + payload length.
	    uint16_t ip_length = 20 + 8 + resp_len;
	    uint16_t total_ip_length = htons(ip_length);
	    
	    struct iphdr *ip = reinterpret_cast<struct iphdr*>(reinterpret_cast<uint8_t*>(frame_data) + sizeof(struct ethhdr));
	    ip->tot_len = total_ip_length;
	    ip->check = 0;
	    ip->check = ntohs(ip_checksum(ip, sizeof(struct iphdr)));
	    
	    // Recalculate IP checksum as needed.
	    struct udphdr *udp = reinterpret_cast<struct udphdr*>(reinterpret_cast<uint8_t*>(frame_data) + sizeof(struct ethhdr) + sizeof(struct iphdr));
	    udp->len = htons(8 + resp_len);
	    udp->check = 0;
	    //udp->check = ntohs( udp_checksum(ip, udp, reinterpret_cast<uint8_t*>(frame_data) + header_size_, resp_len)); 
   	   
	    // The full frame length includes the Ethernet header as well:
    	    uint32_t full_frame_length = header_size_ + resp_len;

	    // Set up the TX descriptor.
	    struct xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&xsk_->tx, tx_idx);
	    tx_desc->addr = frame_addr;
	    tx_desc->len  = full_frame_length;  // Only the response payload is sent.
	    // Submit the TX descriptor so the packet is transmitted.
	    xsk_ring_prod__submit(&xsk_->tx, 1);
	    xsk_->outstanding_tx++;
	    // Optionally, if your configuration requires a kick to flush TX descriptors, do it here.
	    // For example: sendto(xsk_socket__fd(xsk_->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

	    complete_tx();
	    return true;
	}

	void complete_tx()
	{
		unsigned int completed;
		uint32_t idx_cq;

		if (!xsk_->outstanding_tx){
			return;
		}

		sendto(xsk_socket__fd(xsk_->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
		usleep(1000);  // sleep 1ms
		/* Collect/free completed TX buffers */
		completed = xsk_ring_cons__peek(&xsk_->umem->cq,
						XSK_RING_CONS__DEFAULT_NUM_DESCS,
   				&idx_cq);

		if (completed > 0) {
        		for (int i = 0; i < completed; i++) {
            			uint64_t comp_addr = *xsk_ring_cons__comp_addr(&xsk_->umem->cq, idx_cq++);
            			xsk_free_umem_frame(comp_addr);
        		}
			xsk_ring_cons__release(&xsk_->umem->cq, completed);
		 	unsigned int before = xsk_->outstanding_tx;
			xsk_->outstanding_tx -= completed < xsk_->outstanding_tx ? completed : xsk_->outstanding_tx;

		}
	}
	
	bool process_packet(uint64_t addr, uint32_t len)
	{
		uint8_t *pkt = (uint8_t*)xsk_umem__get_data(xsk_->umem->buffer, addr);
		/* Process each recieved packet */	    
		char buffer[FRAME_SIZE]; 
		current_buffer_ = task_handler_->select_next_buffer();
		int buffer_id = current_buffer_;// / 4;
		int thread_id = 0;//current_buffer_;// % 4;
		request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
		
		if (request_->ParseFromArray(pkt, len)) {
		    task_handler_->process_request(buffer_id, 0);  // Simplified
		} else {
			std::cerr << "[ERROR] MatrixRequest::ParseFromArray failed, length: " << len << std::endl;
			// Optionally dump some data for debugging.
			for (uint32_t i = 0; i < std::min(len, 64u); ++i) {
			    std::cerr << std::hex << static_cast<int>(pkt[i]) << " ";
			}
			std::cerr << std::dec << std::endl;
			std::cerr << "[ERROR] Failed to parse request!" << std::endl;
		}

		/*Clean this up later*/
		return true;
	}

	// Function to print Ethernet, IP, and UDP headers from a given buffer.
	void print_headers(const uint8_t* frame, size_t header_size) {
	    // First, print the entire header as hex.
	    print_hex(frame, header_size);

	    // Parse and print Ethernet header.
	    const struct ethhdr* eth = reinterpret_cast<const struct ethhdr*>(frame);
	    std::cout << "Ethernet Header:" << std::endl;
	    std::cout << "  Destination MAC: " << mac_to_string(eth->h_dest) << std::endl;
	    std::cout << "  Source MAC:      " << mac_to_string(eth->h_source) << std::endl;
	    std::cout << "  EtherType:       0x" << std::hex << ntohs(eth->h_proto) << std::dec << std::endl;

	    // Parse and print IP header.
	    const struct iphdr* ip = reinterpret_cast<const struct iphdr*>(frame + sizeof(struct ethhdr));
	    std::cout << "IP Header:" << std::endl;
	    std::cout << "  Version:         " << static_cast<int>(ip->version) << std::endl;
	    std::cout << "  IHL:             " << static_cast<int>(ip->ihl) << std::endl;
	    std::cout << "  Total Length:    " << ntohs(ip->tot_len) << std::endl;
	    char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
	    inet_ntop(AF_INET, &(ip->saddr), src_ip, INET_ADDRSTRLEN);
	    inet_ntop(AF_INET, &(ip->daddr), dst_ip, INET_ADDRSTRLEN);
	    std::cout << "  Source IP:       " << src_ip << std::endl;
	    std::cout << "  Destination IP:  " << dst_ip << std::endl;

	    // Parse and print UDP header.
	    const struct udphdr* udp = reinterpret_cast<const struct udphdr*>(
					  frame + sizeof(struct ethhdr) + sizeof(struct iphdr));
	    std::cout << "UDP Header:" << std::endl;
	    std::cout << "  Source Port:     " << ntohs(udp->source) << std::endl;
	    std::cout << "  Destination Port:" << ntohs(udp->dest) << std::endl;
	    std::cout << "  UDP Length:      " << ntohs(udp->len) << std::endl;
	}


	void handle_receive_packets()
	{
		unsigned int rcvd, stock_frames, i;
		uint32_t idx_rx = 0, idx_fq = 0;
		int ret;
		rcvd = xsk_ring_cons__peek(&xsk_->rx, RX_BATCH_SIZE, &idx_rx);
		if (!rcvd)
			return;

		/* Stuff the ring with as much frames as possible */
		stock_frames = xsk_prod_nb_free(&xsk_->umem->fq,
						xsk_umem_free_frames());

		if (stock_frames > 0) {
			ret = xsk_ring_prod__reserve(&xsk_->umem->fq, stock_frames,
						     &idx_fq);

			/* This should not happen, but just in case */
			while (ret != stock_frames)
				ret = xsk_ring_prod__reserve(&xsk_->umem->fq, rcvd,
							     &idx_fq);

			for (i = 0; i < stock_frames; i++)
				*xsk_ring_prod__fill_addr(&xsk_->umem->fq, idx_fq++) =
					xsk_alloc_umem_frame(xsk_);

			xsk_ring_prod__submit(&xsk_->umem->fq, stock_frames);
		}

		/* Process received packets */
		for (i = 0; i < rcvd; i++) {
			uint64_t addr = xsk_ring_cons__rx_desc(&xsk_->rx, idx_rx)->addr;
			uint32_t len = xsk_ring_cons__rx_desc(&xsk_->rx, idx_rx++)->len;

			if (!process_packet(addr, len))
				xsk_free_umem_frame(addr);

			xsk_->stats.rx_bytes += len;
		}

		xsk_ring_cons__release(&xsk_->rx, rcvd);
		xsk_->stats.rx_packets += rcvd;

		/* Do we need to wake up the kernel for transmission */
		//complete_tx();
	  }

	void rx_and_process()
	{
		struct pollfd fds[2];
		int ret, nfds = 1;

		memset(fds, 0, sizeof(fds));
		fds[0].fd = xsk_socket__fd(xsk_->xsk);
		fds[0].events = POLLIN;

		while(!global_exit) {
			if (cfg_->xsk_poll_mode) {
				ret = poll(fds, nfds, -1);
				if (ret <= 0 || ret > 1)
					std::cout << "ret: " << ret << std::endl;
					continue;
			}
			handle_receive_packets();
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
		rx_and_process();   
	}
		/* Cleanup */
	xsk_socket__delete(xsk_->xsk);
	xsk_umem__delete(umem_->umem);
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
                response_ = (MatrixResponse *)task_handler_->get_buffer_response(buffer_id, 0);
                std::string serialized_response;
                response_->SerializeToString(&serialized_response);

                //memcpy(pkt_data, serialized_response.c_str(), serialized_response.size());
                //desc->len = serialized_response.size();

	    	std::cout << "buffer_id: " << buffer_id << " thread_id: " << thread_id << "response size: " << serialized_response.size() << std::endl; 
                	//xsk_ring_prod__submit(&xsk_->tx, 1);

		send_response(serialized_response.c_str(), serialized_response.size());
		task_handler_->add_resource(thread_id);
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

void RunServer(const std::string &task_type, uint32_t task_size, const std::string &address, config *cfg)
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
    auto server = std::make_unique<DistMultServer>(server_address, 5001, handler.get(), cfg);
   
    while (running)
    {
        
	   std::this_thread::sleep_for(std::chrono::seconds(1));//here I think the issue is....
    }
}
int main(int argc, char **argv)
{
	absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
	absl::InitializeLog();

//	absl::ParseCommandLine(argc, argv);

	if (argc < 4)
	{
	std::cerr << "Usage: " << argv[0] << " <task_type> <n> <<address>>\n";
	return EXIT_FAILURE;
	}

	std::string task_type = argv[1];
	std::string address = argv[3];
	int n = atoi(argv[2]);
	
	int ret;
	const char *ifname = "eth0";   // change to your network interface
	int queue_id = 0;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts, 0);
	struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
	int err;
	char errmsg[1024];
	
	config cfg = {
		.attach_mode = XDP_MODE_UNSPEC,
		.ifindex = -1,
		//.ifname = (char *)cfg.ifname_buf,
		//.ifname_buf = "eth0",
		//.reuse_maps = true,
		//.xsk_if_queue = 0,	
		.do_unload = false, 
		//.prog_id = 0, 
		.filename = "af_xdp_kern.o",
		.progname = "xdp_prog_filter"//,
		//.xsk_poll_mode = true 
		//.src_mac = 0,
		//.dest_mac = 0
		//.xsk_bind_flags = cfg.xsk_bind_flags &= XDP_ZEROCOPY,
	};

	int positional_count = 4; // Number of arguments already used.
	int new_argc = argc - positional_count + 1;
	char **new_argv = argv + positional_count - 1;  // Keep argv[0] as program name
	//signal(SIGNIT, exit_application);

	parse_cmdline_args(argc, argv, long_options, &cfg, __doc__);

	/* Required option */
	if (cfg.ifindex == -1){
		std::cerr << "ERROR: Required option --dev missing" << std::endl;
		usage(argv[0], __doc__, long_options, (argc == 1));
		return -1;
	}

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
			return -1;
		}


		err = xdp_program__attach(prog, cfg.ifindex, cfg.attach_mode, 0);
		if (err) {
			libxdp_strerror(err, errmsg, sizeof(errmsg));
			LOG(INFO) << "Couldn't attach XDP program on iface " << cfg.ifname << ": " << errmsg << ": " << err;
			return -1;
		}

		/* We also need to load the xsks_map */
		map = bpf_object__find_map_by_name(xdp_program__bpf_obj(prog), "xsks_map");
		xsk_map_fd = bpf_map__fd(map);
		if (xsk_map_fd < 0) {
			LOG(INFO) << "ERROR: no xsks map found: " << strerror(xsk_map_fd);
			//exit(EXIT_FAILURE);
			return -1;
		}
	}

	if (task_type == "matrix")
	{
	try
	{
		std::cout<< "Run Server" << std::endl;
		RunServer(task_type, n, address, &cfg);
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

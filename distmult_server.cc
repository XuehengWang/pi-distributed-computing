#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <vector>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
//#include <rte_flow.h>

#include "distmult_service.pb.h"
#include "task_handler.h"
#include "matrix_handler.h"

using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;

std::atomic<bool> running(true); // Flag to control the server loop


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define SERVER_IP "192.168.1.127"
uint16_t portid = 0;

class DistMultServer
{
public:
    explicit DistMultServer(const std::string &ip_address, int port, TaskHandler *handler)
        : task_handler_(handler), stop_reader_thread_(false), stop_writer_thread_(false)
    {

	    mbuf_pool_ = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	    if (!mbuf_pool_) {
		std::cerr << "[ERROR] Failed to create DPDK memory pool!\n";
		return;// -1;
	    }
	   
	    struct rte_eth_conf port_conf = {
		    .rxmode = {
			.mq_mode = RTE_ETH_MQ_RX_NONE,  // Single queue mode
		    },
		    .txmode = {
			.mq_mode = RTE_ETH_MQ_TX_NONE,  // Single queue mode
		    }
	 
	    };
	    
	    uint16_t num_ports = rte_eth_dev_count_avail();
	    if (num_ports == 0) {
	        std::cerr << "[ERROR] No available DPDK-compatible ports found!\n";
	        return; //-1;
	    }
	    std::cout << "[INFO] Found " << num_ports << " DPDK-compatible ports.\n";


	    if (rte_eth_dev_configure(portid, 1, 1, &port_conf) < 0) {
		std::cerr << "[ERROR] Failed to configure Ethernet device!\n";
		return;// -1;
	    }

	    if (rte_eth_rx_queue_setup(portid, 0, RX_RING_SIZE, rte_eth_dev_socket_id(portid), nullptr, mbuf_pool_) < 0 ||
		rte_eth_tx_queue_setup(portid, 0, TX_RING_SIZE, rte_eth_dev_socket_id(portid), nullptr) < 0) {
		std::cerr << "[ERROR] Failed to set up RX/TX queues!\n";
		return;// -1;
	    }

	    if (rte_eth_dev_start(portid) < 0) {
		std::cerr << "[ERROR] Failed to start Ethernet device!\n";
		return;// -1;
	    }

	    std::cout << "[INFO] DPDK UDP Server is running...\n";
           
           //setup_ip_filter(portid);
	
        handler->initialize_buffers();
        if (!stop_reader_thread_)
        {
            std::cout << "Start reader and writer threads" << std::endl;
            reader_thread_ = std::thread(&DistMultServer::start_reading, this);
            writer_thread_ = std::thread(&DistMultServer::writer_thread, this, ip_address, port);
        
	    pin_thread_to_core(reader_thread_, 0);
	    pin_thread_to_core(writer_thread_, 0);
	}
    }

    ~DistMultServer()
    {
        stop();
    }

private:
    struct rte_mempool *mbuf_pool_;
    bool stop_writer_thread_;
    bool stop_reader_thread_;
    std::thread reader_thread_;
    std::thread writer_thread_;
    TaskHandler *task_handler_;
    std::mutex task_lock_;
    int current_buffer_;
    
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
   /** 
	void setup_ip_filter(uint16_t port) {
	    struct rte_flow_attr attr = {};
	    struct rte_flow_item pattern[2] = {};
	    struct rte_flow_action action[1] = {};
	    struct rte_flow_error error;

	    struct rte_flow_action_queue queue = { .index = 0 };  // Send matched packets to queue 0

	    // 🔹 Match packets destined for `192.168.1.127`
	    struct rte_flow_item_ipv4 ip_spec = {};
	    struct rte_flow_item_ipv4 ip_mask = {};
	    inet_pton(AF_INET, SERVER_IP, &ip_spec.hdr.dst_addr);
	    ip_mask.hdr.dst_addr = 0xFFFFFFFF;  // Exact match

	    pattern[0].type = RTE_FLOW_ITEM_TYPE_IPV4;
	    pattern[0].spec = &ip_spec;
	    pattern[0].mask = &ip_mask;

	    // 🔹 Action: Queue the packet (instead of dropping it)
	    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
	    action[0].conf = &queue;

	    // 🔹 Create the flow rule
	    struct rte_flow *flow = rte_flow_create(port, &attr, pattern, action, &error);
	    if (!flow) {
		std::cerr << "[ERROR] Flow rule creation failed: " << error.message << std::endl;
	    } else {
		std::cout << "[INFO] Successfully created flow rule for IP " << SERVER_IP << std::endl;
	    }
	}
**/
    void start_reading(){
	struct rte_mbuf *bufs[BURST_SIZE];
    	while(!stop_writer_thread_){
		uint16_t nb_rx = rte_eth_rx_burst(portid, 0, bufs, BURST_SIZE);
		if(nb_rx == 0){
			//deal with closing socket here
		      /**
		      current_buffer_ = task_handler_->select_next_buffer();
                      int buffer_id = current_buffer_;// / 4;
                      int thread_id = 0;//current_buffer_;// % 4;
                      
		      std::cout << "buffer_id: " << buffer_id << " thread_id: " << thread_id << std::endl; 
		      request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
                      request_->set_task_id(-1);
 	              task_handler_->process_request(buffer_id, thread_id);

		      stop_writer_thread_ = true;
		      stop_reader_thread_ = true;
		      **/
		      return;
		}
		else if(nb_rx > 0){
			for (uint16_t i = 0; i < nb_rx; i++){
			   struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
			   struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
			   
			   struct in_addr dest_ip;
			   dest_ip.s_addr = ip_hdr->dst_addr;

			   if (strcmp(inet_ntoa(dest_ip), SERVER_IP) == 0){
			  	   std::cout << "[INFO] Packet received for " << SERVER_IP << std::endl;
				   struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);			   

				   char *payload = (char *)(udp_hdr + 1);
				   uint16_t data_len = ntohs(udp_hdr->dgram_len) - sizeof(struct rte_udp_hdr);

				   if(ip_hdr->next_proto_id == IPPROTO_UDP){
				       current_buffer_ = task_handler_->select_next_buffer();
				       int buffer_id = current_buffer_;// / 4;
				       int thread_id = 0;//current_buffer_;// % 4;
				       std::cout << "current buffer is: " << current_buffer_ << std::endl; 
				       request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
					if (!request_->ParseFromArray(payload, data_len)){
					    std::cerr << "Failed to parse protobuf message" << std::endl;
					    return;
					}
					else
					{
					    std::cout << "task_id is: " << request_->task_id() << std::endl;
					    task_handler_->process_request(buffer_id, thread_id);
					}
					break;
				    }
						//process request
				    std::cout << "Server received UDP packet" << std::endl;

					// Echo back the received message
			    }else {
			           std::cout << "[INFO] Ignoring packet for " << inet_ntoa(dest_ip) << std::endl;
			    }
			    rte_pktmbuf_free(bufs[i]);  // Free received packet
			}
		} else {
			//catch some error here!!
		}
	}
}
    
void writer_thread(const std::string& ip_address, int dst_port){
	while (!stop_writer_thread_){
	
	    int response_id = task_handler_->check_response();
            if (response_id == -1)
            {
                LOG(INFO) << "OHNO Server writer: check response returns -1";
            }
            else {

                int buffer_id = response_id;// / 4;
                int thread_id = 0;// response_id % 4;
		std::cout << "buffer_id: " << buffer_id << " thread_id: " << thread_id << std::endl; 
		response_ = (MatrixResponse *)task_handler_->get_buffer_response(buffer_id, thread_id);
		
                std::string serialized_response;
                response_->SerializeToString(&serialized_response);
		
		struct rte_mbuf *pkt = rte_pktmbuf_alloc(mbuf_pool_);
		if (!pkt){
			std::cerr << "Packet allocation failed";
			return;
		}
		try {
			struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
			struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr*)(eth_hdr + 1);
			struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
			char *payload = (char *)(udp_hdr + 1);

			//Fill Ethernet header
			rte_eth_macaddr_get(portid, &eth_hdr->src_addr);
			memset(&eth_hdr->dst_addr, 0xFF, sizeof(eth_hdr->dst_addr));//Broadcast

			eth_hdr->ether_type = htons(RTE_ETHER_TYPE_IPV4);
			struct in_addr dst_ip;
			inet_pton(AF_INET, ip_address.c_str(), &dst_ip);
			//Fill IP header
			ip_hdr->version_ihl = (4 << 4) | 5;
			ip_hdr->total_length = htons(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + serialized_response.size());
			ip_hdr->next_proto_id = IPPROTO_UDP;
			ip_hdr->src_addr = inet_addr(SERVER_IP);  // Server IP
			ip_hdr->dst_addr = dst_ip.s_addr;

			// Fill UDP header
			udp_hdr->src_port = htons(8080);
			udp_hdr->dst_port = htons(dst_port);
			udp_hdr->dgram_len = htons(sizeof(struct rte_udp_hdr) + serialized_response.size());

			//copy response data
			memcpy(payload, &serialized_response, serialized_response.size());

			//set packet lengths
			pkt->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + serialized_response.size();
			pkt->pkt_len = pkt->data_len;

			//send response
			struct rte_mbuf *tx_pkts[1] = {pkt};
			if (rte_eth_tx_burst(portid, 0, tx_pkts, 1) == 0) {
				std::cerr << "[ERROR] Failed to send UDP response!\n";
				rte_pktmbuf_free(pkt);
			}
			task_handler_->add_resource(thread_id);
		}catch (const std::exception &e) {
		        std::cerr << "[EXCEPTION] Error sending response: " << e.what() << "\n";
        		rte_pktmbuf_free(pkt);
		}	
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

    // Extract DPDK-specific arguments
    std::vector<char *> dpdk_args;
    dpdk_args.push_back(argv[0]);  // Program name

    // Define default DPDK arguments
    dpdk_args.push_back((char *)"-l"); dpdk_args.push_back((char *)"0-1");  // Use cores 0 and 1
    dpdk_args.push_back((char *)"-n"); dpdk_args.push_back((char *)"2");    // Use 2 memory channels
    dpdk_args.push_back((char *)"--vdev=net_af_packet0,iface=eth0");        // Use software-based network interface
    dpdk_args.push_back((char *)"--no-huge");
    // Initialize DPDK with extracted arguments
    int dpdk_argc = dpdk_args.size();
    if (rte_eal_init(dpdk_argc, dpdk_args.data()) < 0) {
        std::cerr << "[ERROR] DPDK EAL initialization failed!\n";
        return EXIT_FAILURE;
    }

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

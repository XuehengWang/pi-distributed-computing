#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>

//Define the target ip address
#define TARGET_IP 0xC0A80180

//This map holds the file descriptors for AF_XDP socekts.
//Packets redirected with bfp_redirect_map() use this map to determine
//which socket (and queue) should recieve the packet
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

SEC("xdp_filter")
int xdp_prog_filter(struct xdp_md *ctx){
	//Get pointers to the beginning and end of the packet data.
	void *data = (void *)(unsigned long)ctx->data;
        void *data_end = (void *)(unsigned long)ctx->data_end;
	
	//int index = ctx->rx_queue_index;
	//_u32 *pkt_count;
	//
	//pkt_count = bpf_map_lookup_elem(&xdp_stats_map, &index);
	//if (pkt_count) {
	//	/** Do something here*/
	//	return XDP_PASS;
	//}

	//Parse Ethernet
	struct ethhdr *eth = data;
	if ((void*)(eth + 1) > data_end)
		return XDP_PASS;

	//Only process IPv4 packets.
	if (eth->h_proto != __constant_htons(ETH_P_IP))
		return XDP_PASS;

	//Parse IP header.
	struct iphdr *ip = data + sizeof(struct ethhdr);
	if ((void *)(ip + 1) > data_end)
		return XDP_PASS;

	//Check if the source IP matches our target.
	//Note: ip->saddr is in network byte order, so we convert TARGET_IP.
	if(ip->saddr == __constant_htonl(TARGET_IP)){
		// For this example, we use queue index 0 in the xsks_map
		int index = 0;
		//Redirect the packet to the AF_XDF socket via the xsks_map.
		return bpf_redirect_map(&xsks_map, index, 0);
	}

	//If it doesn't match, let the kernel handle it normally.
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

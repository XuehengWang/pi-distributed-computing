#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>   // This defines struct udphdr
#include <bpf/bpf_helpers.h>

// Define the target IP address (192.168.1.128)
#define TARGET_IP 0xC0A80180

// Map holding the file descriptors for AF_XDP sockets.
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 64);
} xdp_stats_map SEC(".maps");

SEC("xdp_filter")
/**
int xdp_prog_filter(struct xdp_md *ctx) {
    // Get initial pointers to packet data.
    void *data = (void *)(unsigned long)ctx->data;
    void *data_end = (void *)(unsigned long)ctx->data_end;

    // Parse Ethernet header.
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    
    // Check if this is an ARP packet.
    if (eth->h_proto == __constant_htons(ETH_P_ARP)) {
        bpf_trace_printk("Ignoring ARP packet\n", sizeof("Ignoring ARP packet\n"));
        return XDP_PASS;
    }    

    // Check if the packet is IPv4.
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;

    // Save Ethernet header size.
    int eth_hdr_size = sizeof(struct ethhdr);

    // Parse IP header (still in the original data, before adjustment).
    struct iphdr *ip = data + eth_hdr_size;
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    // Check if the source IP matches TARGET_IP.
    if (ip->saddr != __constant_htonl(TARGET_IP))
        return XDP_PASS;

    // *** Remove the Ethernet header ***
//    if (bpf_xdp_adjust_head(ctx, eth_hdr_size) < 0)
//       return XDP_ABORTED;

    // After adjusting, update data pointers.
//    data = (void *)(unsigned long)ctx->data;
//    data_end = (void *)(unsigned long)ctx->data_end;

    // Now the packet begins with the IP header.
//    ip = data;
//    if ((void *)(ip + 1) > data_end)
//        return XDP_ABORTED;

    // Calculate IP header length (ihl is in 32-bit words).
//    int ip_hdr_len = ip->ihl * 4;
//    if (ip_hdr_len < sizeof(struct iphdr))
//        return XDP_ABORTED;

    // *** Remove the IP header ***
//    if (bpf_xdp_adjust_head(ctx, ip_hdr_len) < 0)
//        return XDP_ABORTED;

    // Update pointers one last time.
//    data = (void *)(unsigned long)ctx->data;
//    data_end = (void *)(unsigned long)ctx->data_end;

    // At this point, all network headers (Ethernet and IP) have been removed.
    // The packet now starts at the transport layer header (e.g., UDP).
//    bpf_trace_printk("Headers removed, redirecting packet\n",
//                     sizeof("Headers removed, redirecting packet\n"));

    // Redirect the packet to userspace via the appropriate queue.
    int index = ctx->rx_queue_index;
    return bpf_redirect_map(&xsks_map, index, 0);
}
**/
int xdp_prog_filter(struct xdp_md *ctx) {
    void *data = (void *)(unsigned long)ctx->data;
    void *data_end = (void *)(unsigned long)ctx->data_end;

    // Parse Ethernet header.
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    bpf_trace_printk("eth->h_proto: %x\n", sizeof("eth->h_proto: %x\n"), eth->h_proto);

    int eth_hdr_size = sizeof(struct ethhdr);

    // Check if this is an ARP packet.
    if (eth->h_proto == __constant_htons(ETH_P_ARP)) {
        bpf_trace_printk("Ignoring ARP packet\n", sizeof("Ignoring ARP packet\n"));
        return XDP_PASS;
    }

    // Process only IPv4 packets.
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;

    // Parse IP header (using the current pointer before adjustment).
    struct iphdr *ip = data + eth_hdr_size;
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    // Check if the source IP matches TARGET_IP.
    if (ip->saddr != __constant_htonl(TARGET_IP))
        return XDP_PASS;

    __u32 src_ip = __builtin_bswap32(ip->saddr);
    bpf_trace_printk("Source IP: %x\n", sizeof("Source IP: %x\n"), src_ip);

    // --- Remove Ethernet header ---
    if (bpf_xdp_adjust_head(ctx, eth_hdr_size) < 0)
        return XDP_ABORTED;

    // Update data pointers after stripping Ethernet header.
    data = (void *)(unsigned long)ctx->data;
    data_end = (void *)(unsigned long)ctx->data_end;

    // --- Re-read IP header ---
    ip = data;
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    int ip_hdr_size = ip->ihl * 4;
    if ((char *)data + ip_hdr_size > (char *)data_end)
        return XDP_PASS;

    // Strip the IP header.
    if (bpf_xdp_adjust_head(ctx, ip_hdr_size) < 0)
        return XDP_ABORTED;

    // Update pointers after stripping IP header.
    data = (void *)(unsigned long)ctx->data;
    data_end = (void *)(unsigned long)ctx->data_end;

    // --- Now remove UDP header ---
    //struct udphdr *udp = data;
    int udp_hdr_size = sizeof(struct udphdr);
    if ((char *)data + udp_hdr_size > (char *)data_end)
        return XDP_PASS;

    // Strip the UDP header.
    if (bpf_xdp_adjust_head(ctx, udp_hdr_size) < 0)
        return XDP_ABORTED;

    // Update pointers after stripping UDP header.
    data = (void *)(unsigned long)ctx->data;
    data_end = (void *)(unsigned long)ctx->data_end;
    int payload_len = (char *)data_end - (char *)data;

    // For debugging: print the UDP payload length.
    bpf_trace_printk("UDP payload length: %d\n",
                     sizeof("UDP payload length: %d\n"),
                     payload_len);

    // Redirect the packet to user space via the xsks_map.
    int index = ctx->rx_queue_index; // or use a fixed key if you have one
    return bpf_redirect_map(&xsks_map, index, 0);
    //return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

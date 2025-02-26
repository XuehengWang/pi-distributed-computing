#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_prog(struct __sk_buff *skb) {
    void *data_end = (void *)(long)skb->data_end;
    void *data = (void *)(long)skb->data;
    struct ethhdr *eth = data;

    if (data + sizeof(struct ethhdr) > data_end)
        return XDP_DROP;  // Drop packet if header is incomplete

    if (eth->h_proto == __constant_htons(ETH_P_IP)) {
        struct iphdr *ip = data + sizeof(struct ethhdr);
        if ((void *)ip + sizeof(struct iphdr) > data_end)
            return XDP_DROP;

        if (ip->protocol == IPPROTO_UDP) {
            return XDP_REDIRECT;  // Redirect UDP packets to AF_XDP socket
        }
    }

    return XDP_PASS;  // Let non-UDP packets continue normally
}

char _license[] SEC("license") = "GPL";


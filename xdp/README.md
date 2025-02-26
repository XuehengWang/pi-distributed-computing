Compile using:

clang -O2 -target bpf -c xdp_prog.c -o xdp_prog.o

Load it to an interface:

ip link set dev eth0 xdp obj xdp_prog.o


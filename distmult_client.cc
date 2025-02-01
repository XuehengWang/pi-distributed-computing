#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define PORT 8080
#define MAX_EVENTS 10
#define CONNECTION_TIMEOUT 5
#define EPOLL_TIMEOUT 5000
#define TERMINATION_MESSAGE "EXIT"

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "task_handler.h"
#include "matrix_handler.h"
#include "utils.h"

using utils::MatrixRequest;
using utils::MatrixResponse;
using matrixclass::MatrixClass;

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

class DistMultImpl {
public:
    explicit DistMultImpl(TaskHandler* handler) : task_handler_(handler) {}

    void ProcessJob(const MatrixRequest& request, int sock) {
    	int buffer_id = task_handler_->select_next_buffer();
    	if (buffer_id == -1) {
        	std::cerr << "ERROR: No buffer available for processing!" << std::endl;
        	return;
    	}

    	// Get the buffer location and copy the request into it
    	void* buffer = task_handler_->get_buffer_request(buffer_id, 0); // Assume single-threaded for now
    	if (!buffer) {
        	std::cerr << "ERROR: Failed to retrieve buffer!" << std::endl;
        	return;
    	}

    	// Copy request data into buffer (assuming raw memory storage)
    	std::memcpy(buffer, &request, sizeof(MatrixRequest));

    	// Process request
    	task_handler_->process_request(buffer_id, 0); // Process request in the selected buffer

    	// Retrieve response
    	int response_id = task_handler_->check_response();
    	if (response_id != -1) {
        	MatrixResponse* response = static_cast<MatrixResponse*>(task_handler_->get_buffer_response(response_id / 4, response_id % 4));
        	send(sock, response, sizeof(MatrixResponse), 0);
    	}
    }

private:
    TaskHandler* task_handler_;
};

void RunServer(const std::string& task_type, uint32_t task_size, const std::string& address) {
    std::unique_ptr<TaskHandler> handler;
    if (task_type == "matrix") {
        handler = std::make_unique<MatrixClass>(task_size);
    } else {
        std::cerr << "Unsupported task type: " << task_type << std::endl;
        return;
    }

    DistMultImpl service(handler.get());

    int sock, epoll_fd;
    sockaddr_in server_addr;
    epoll_event event, events[MAX_EVENTS];
    struct timeval timeout;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    set_nonblocking(sock);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(address.c_str());

    if (connect(sock, (sockaddr *)&server_addr, sizeof(server_addr)) < 0 && errno == EINPROGRESS) {
        timeout.tv_sec = CONNECTION_TIMEOUT;
        timeout.tv_usec = 0;
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);

        if (select(sock + 1, NULL, &write_fds, NULL, &timeout) <= 0) {
            perror("Connection timeout");
            close(sock);
            return;
        }
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Epoll create failed");
        exit(EXIT_FAILURE);
    }
    event.data.fd = sock;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &event);

    std::cout << "Connected to server. Waiting for tasks..." << std::endl;
    
    while (true) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT); //what does this function do - we don't want to wait until we have "10" events.
        if (num_events == 0) continue;
        
        for (int i = 0; i < num_events; i++) {//does this loop run indefinitely
            if (events[i].events & EPOLLIN) {
                MatrixRequest request; //create request object to write the request into. 
                ssize_t bytes_received = recv(sock, &request, sizeof(MatrixRequest), 0);
                if (bytes_received > 0) {
                    if (request.task_id == -1) {
                        std::cout << "Termination message received. Closing client." << std::endl;
                        close(sock);
                        return;
                    }
                    service.ProcessJob(request, sock);
                } else if (bytes_received == 0) {
                    std::cout << "Server disconnected." << std::endl;
                    break;
                }
            }
        }
    }
    close(sock);
}

int main(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::InitializeLog();
    
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <task_type> <n> <address>\n";
        return EXIT_FAILURE;
    }

    std::string task_type = argv[1];
    std::string address = argv[3];
    int n = atoi(argv[2]);

    if (task_type == "matrix") {
        try {
            RunServer(task_type, n, address);
        } catch (const std::exception& e) {
            std::cerr << "Error running server: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    } else {
        std::cerr << "Error: Unsupported task type \"" << task_type << "\". Supported: \"matrix\".\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


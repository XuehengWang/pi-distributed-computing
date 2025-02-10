#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include "distmult_service.pb.h"
#include "task_handler.h"
#include "matrix_handler.h"

using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

class DistMultServer {
public:
    explicit DistMultServer(const std::string& ip_address, int port, TaskHandler* handler) 
        : task_handler_(handler), client_socket_(-1), stop_writer_thread_(false) {
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr) <= 0) {
            perror("Invalid address");
            exit(EXIT_FAILURE);
        }
        if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Binding failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_socket_, 1) < 0) { // Only one client
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }
	char str[INET_ADDRSTRLEN];
        std::cout << "Server connected:" << inet_ntop(AF_INET,&server_addr.sin_addr,
                    str, INET_ADDRSTRLEN) << std::endl;
	handler->initialize_buffers();
        //stop_writer_thread_ = true;
	client_socket_ = accept_client();
        //close(server_socket_);
        if (client_socket_ != -1) {
	    std::cout << "Start reader and writer threads" << std::endl;
            reader_thread_ = std::thread(&DistMultServer::start_reading, this);
            writer_thread_ = std::thread(&DistMultServer::writer_thread, this);
        }
    }

    ~DistMultServer() {
        stop();
    }

private:
    int server_socket_;
    int client_socket_;
    bool stop_writer_thread_;
    std::thread reader_thread_;
    std::thread writer_thread_;
    TaskHandler* task_handler_;
    std::mutex task_lock_;
    int current_buffer_;

    int accept_client() {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int new_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (new_socket < 0) {
            perror("Accept failed");
            return -1;
        }
	char str[INET_ADDRSTRLEN];
        std::cout << "Client connected:" << inet_ntop(AF_INET,&client_addr.sin_addr,
                    str, INET_ADDRSTRLEN) << std::endl;
        return new_socket;
    }

    void start_reading() {
        while (client_socket_ != -1) {
	    struct pollfd pfd;
	    pfd.fd = client_socket_;
	    pfd.events = POLLIN;  // Wait for incoming data

	    while (client_socket_ != -1){
		int ret = poll(&pfd, 1, 5000);
		if (ret > 0 && (pfd.revents & POLLIN)) {
		    uint32_t size;
		    recv(client_socket_, &size, sizeof(size), MSG_WAITALL);
		    size = ntohl(size);

		    current_buffer_ = task_handler_->select_next_buffer();
		    int buffer_id = current_buffer_ / 4;
		    int thread_id = current_buffer_ % 4;
		    request_ = (MatrixRequest*)task_handler_->get_buffer_request(buffer_id, thread_id);
		    std::string buffer(size, 0);
		    recv(client_socket_, &buffer[0], size, MSG_WAITALL);

		    if (!request_->ParseFromString(buffer)) {
			std::cerr << "Failed to parse protobuf message" << std::endl;
		    } else {
			std::cout << "Received Task ID: " << request_->task_id() << std::endl;
		        task_handler_->process_request(buffer_id, thread_id);
		    }
		    break;
		} else if (ret == 0) {
		    std::cout << "No data received, sleeping..." << std::endl;
		    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		} else {
		    //std::cerr << "poll() error" << std::endl;
		    break;
		}

		   // std::cout << "Client connected" << std::endl;
	    
	    }
	    
    	}
        
    }

    void writer_thread() {
	std::cout << "Started writer Thread " << std::endl;
	//std::cout << stop_writer_thread_ << std::endl;
        stop_writer_thread_ = false;
	while (!stop_writer_thread_) {
            int response_id = task_handler_->check_response();
	    std::cout << "The response id is: " << response_id << std::endl;
	    if (response_id == -1) {
		LOG(INFO) << "OHNO Server writer: check response returns -1";	    
	    } else {
	//	    std::cout << "We are sending a message" << std::endl; 
		    int buffer_id = response_id / 4;
		    int thread_id = response_id % 4;
		    response_ = (MatrixResponse*)task_handler_->get_buffer_response(buffer_id, thread_id);
		    
		    std::string serialized_response;
		    response_->SerializeToString(&serialized_response);
		    uint32_t size = htonl(serialized_response.size());
		    std::cout << "The size of the response is : " << size << std::endl;
		    std::string final_message; //can we combine so this just uses one string
		    final_message.append(reinterpret_cast<const char*>(&size), sizeof(size));  // Prefix with size
		    final_message.append(serialized_response);  // Append protobuf data
		    
		    if (client_socket_ != -1) {
			if (send(client_socket_, final_message.c_str(), final_message.size(), 0) == -1) {
			    perror("Failed to send response");
			}
		    }
	    }
        }
    }

    void stop() {
        stop_writer_thread_ = true;
        std::cerr << "Closing server socket at line " << __LINE__ << std::endl;
	close(server_socket_);
        if (client_socket_ != -1) {
          std::cerr << "Closing client socket at line " << __LINE__ << std::endl;  
	  close(client_socket_);
        }
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        std::cout << "Server stopped" << std::endl;
    }

    MatrixRequest* request_;
    MatrixResponse* response_;
};

void RunServer(const std::string& task_type, uint32_t task_size, const std::string& address){
	
	std::string server_address(address);
	//create task handler
	std::unique_ptr<TaskHandler> handler;

	if (task_type == "matrix") {
	    //handler = new MatrixClass(task_size);
	    handler = std::make_unique<MatrixClass>(task_size);
	  } else {
	    std::cerr << "Unsupported task type: " << task_type << std::endl;
	    return;
	  }

    // Use unique_ptr to manage server lifetime
    auto server = std::make_unique<DistMultServer>(server_address, 5001, handler.get());

    std::cout << "Server is running..." << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();
  
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <task_type> <n> <<address>>\n";
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

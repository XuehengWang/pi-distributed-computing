#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
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
    explicit DistMultServer(int port, TaskHandler* handler) 
        : task_handler_(handler), client_socket_(-1), stop_writer_thread_(false) {
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Binding failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_socket_, 1) < 0) { // Only one client
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }

        handler->initialize_buffers();
        
        client_socket_ = accept_client();
        if (client_socket_ != -1) {
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
    std::thread reader_thread_;
    std::thread writer_thread_;
    bool stop_writer_thread_;
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
        std::cout << "Client connected" << std::endl;
        return new_socket;
    }

    void start_reading() {
        while (client_socket_ != -1) {
            char buffer[4096];
            int bytes_received = recv(client_socket_, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                std::cerr << "Client disconnected." << std::endl;
                client_socket_ = -1;
                return;
            }

            MatrixRequest request;
            request.ParseFromArray(buffer, bytes_received);

            current_buffer_ = task_handler_->select_next_buffer();
            if (current_buffer_ == -1) {
                std::cerr << "ERROR: No buffer available for receiving new request!" << std::endl;
            } else {
                int buffer_id = current_buffer_ / 4;
                int thread_id = current_buffer_ % 4;
                request_ = (MatrixRequest*)task_handler_->get_buffer_request(buffer_id, thread_id);
                *request_ = request;
                task_handler_->process_request(buffer_id, thread_id);
            }
        }
    }

    void writer_thread() {
        while (!stop_writer_thread_) {
            int response_id = task_handler_->check_response();
            if (response_id == -1) continue;

            int buffer_id = response_id / 4;
            int thread_id = response_id % 4;
            response_ = (MatrixResponse*)task_handler_->get_buffer_response(buffer_id, thread_id);
            
            std::string serialized_response;
            response_->SerializeToString(&serialized_response);

            if (client_socket_ != -1) {
                if (send(client_socket_, serialized_response.c_str(), serialized_response.size(), 0) == -1) {
                    perror("Failed to send response");
                }
            }
        }
    }

    void stop() {
        stop_writer_thread_ = true;
        close(server_socket_);
        if (client_socket_ != -1) {
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

	DistMultServer server(5001, handler.get()); //pass in the rest of the parameters here

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

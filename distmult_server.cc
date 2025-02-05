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
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include "distmult_service.pb.h"
#include "task_handler.h"
#include "matrix_handler.h"

using boost::asio::ip::tcp;
using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

class DistMultServer {
public: 
	explicit DistMultServer(boost::asio::io_context &io_context, int port, TaskHandler* handler) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), 
	ssl_context_(boost::asio::ssl::context::tlsv12), task_handler_(handler), client_socket_(nullptr), stop_writer_thread_(false)
	{
        ssl_context_.set_options(boost::asio::ssl::context::default_workarounds);
        //ssl_context_.use_certificate_chain_file("server.crt");
        //ssl_context_.use_private_key_file("server.key", boost::asio::ssl::context::pem);
	
	handler->initialize_buffers();
	start_accept();	
	writer_thread_ = std::thread(&DistMultServer::writer_thread, this);	
	}


	~DistMultServer(){
      		if (writer_thread_.joinable()) {
       		 writer_thread_.join();
      	}
	}
	
private:
    tcp::acceptor acceptor_;
    boost::asio::ssl::context ssl_context_;
   // DistMultService distMult_;

    void start_accept() {
        auto socket = std::make_shared<boost::asio::ssl::stream<tcp::socket>>(acceptor_.get_executor(), ssl_context_);
        acceptor_.async_accept(socket->lowest_layer(), [this, socket](boost::system::error_code ec) {
            if (!ec) {
                socket->async_handshake(boost::asio::ssl::stream_base::server, [this, socket](boost::system::error_code handshake_ec) {
                    if (!handshake_ec) {
                      client_socket_ = socket;    
		      //handle_client(socket);
                      start_reading();
		    }
                });
            }
            start_accept(); //Supports reconnects
        });
    }

    void start_reading(){
    	if(!client_socket_) return;
	current_buffer_ = task_handler_->select_next_buffer();
	if (current_buffer_ == -1){
		std::cerr << "ERROR: No buffer available for receiving new request!" << std::endl;
	} else {
	//	int buffer_id = current_buffer_/4;
	//	int thread_id = current_buffer_ % 4;

	//	request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
		auto buffer = std::make_shared<std::vector<char>>(1024);
		client_socket_->async_read_some(boost::asio::buffer(*buffer),
		    [this, buffer](boost::system::error_code ec, std::size_t length) {
			if (!ec) {
			     
			    int buffer_id = current_buffer_/4;
			    int thread_id = current_buffer_ % 4;
			    request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
			    //Here we get the request
			    request_->ParseFromArray(buffer->data(), length);
			    //send the request to the task handler 
			    task_handler_->process_request(buffer_id, thread_id);
			    //read new requests
			    start_reading();
			} else {
			    std::cerr << "Client disconnected.\n";
			    client_socket_ = nullptr;
			}
		    });
		}
    }

   void writer_thread(){
   	while (!stop_writer_thread_){
	    int all_id = task_handler_->check_response();

	    if(all_id == -1){ //no more responses to process
	      LOG(INFO) << "OHNO ServerweriterL check response returns -1";

	    } else {
	    	int buffer_id = all_id / 4;
		int thread_id = all_id % 4;
		response_ = (MatrixResponse *) task_handler_->get_buffer_response(buffer_id, thread_id);
		//Start Wrtie

		auto response_buffer = std::make_shared<std::string>();
		response_->SerializeToString(response_buffer.get());

		boost::asio::async_write(*client_socket_, boost::asio::buffer(*response_buffer),
		    [response_buffer](boost::system::error_code ec, std::size_t) {
			if (ec) {
			    std::cerr << "Error sending response.\n";
			}
		    });
	    }
        }
   } 

    TaskHandler* task_handler_;
    bool stop_writer_thread_;
    std::mutex mu_;
    std::queue<MatrixRequest> request_queue_;
    int current_buffer_;
   //Need an actual buffer to read these into: perhaps we can use the message_queue but I will check the task_handler
    //void* request_;
    //void* response_;
    MatrixRequest* request_;
    MatrixResponse* response_;
    //std::queue<Request> message_queue_;
    //std::vector<char> response_buffer_{1024};
    std::shared_ptr<boost::asio::ssl::stream<tcp::socket>> client_socket_;
    std::thread writer_thread_;
    std::atomic<bool> stop_threads_{false};

};

void RunServer(const std::string& task_type, uint32_t task_size, const std::string& address){
	
	std::string server_address(address);
	boost::asio::io_context io_context;
	//create task handler
	std::unique_ptr<TaskHandler> handler;

	if (task_type == "matrix") {
	    //handler = new MatrixClass(task_size);
	    handler = std::make_unique<MatrixClass>(task_size);
	  } else {
	    std::cerr << "Unsupported task type: " << task_type << std::endl;
	    return;
	  }

	DistMultServer server(io_context, 8080, handler.get()); //pass in the rest of the parameters here
	io_context.run();	

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

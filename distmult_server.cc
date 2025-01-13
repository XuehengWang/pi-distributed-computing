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
//#include <cstdlib>   // std::atoi

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "distmult_service.pb.h"
#include "distmult_service.grpc.pb.h"

//#include "function_map.h"
#include "task_handler.h"
#include "matrix_handler.h"

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
// using grpc::ServerContext;
// using grpc::ServerReaderWriter;
// using grpc::ServerWriter;
using grpc::Status;
using grpc::CallbackServerContext;
using grpc::ServerBidiReactor;
using distmult::DistMultService;
using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;


class DistMultImpl final : public DistMultService::CallbackService {
public:
    explicit DistMultImpl(TaskHandler* handler) : task_handler_(handler) {}

    ~DistMultImpl() {
      
    }
    
    grpc::ServerBidiReactor<MatrixRequest, MatrixResponse>* ComputeMatrix(
      CallbackServerContext* context) override {
    class ComputeRPC : public grpc::ServerBidiReactor<MatrixRequest, MatrixResponse> {
    public:
    ComputeRPC(TaskHandler* handler, std::mutex* mu)
          : mu_(mu), task_handler_(handler), current_buffer_(-1){
        
        handler->initialize_buffers();
        // start with read
        writer_thread_ = std::thread(&ComputeRPC::writer, this);
        NextRead();
        
    }
    ~ComputeRPC() {
      // stop_threads_ = true;
      if (writer_thread_.joinable()) {
        writer_thread_.join();
      }
    }

    /* 
    server-side reader and writer threads
    reader (OnReadDone) -> process received request, assign tasks to compute threads, notify compute threads
    writer: get results from compute threads, prepare MatrixReponse, write it back
    someone Mr.X: decide next compute thread
    */
      // call here after NextRead, current_buffer_ should not change
      void OnReadDone(bool ok) override {
        if (ok) {
          //LOG(INFO) << "IO reader: Finish reading a task from RPC, current_buffer_ is " << current_buffer_;
          int buffer_id = current_buffer_ / 4;
          int thread_id = current_buffer_ % 4;
          task_handler_->process_request(buffer_id, thread_id);
          //LOG(INFO) << "IO reader: Already assigned task to thread " << thread_id << " buffer " << buffer_id;
          //check write
          //UPDATE: NextWrite();
          NextRead();
        } else {
          Finish(Status::OK);
        }
      }

      void OnWriteDone(bool /*ok*/) override { NextWrite(); }

      void OnDone() override {
        LOG(INFO) << "RPC Completed";
        delete this;
      }

      void OnCancel() override { LOG(ERROR) << "RPC Cancelled"; }

      void writer() { 
        //while (!stop_threads_) {
          NextWrite();
        //}
      }

    private:
    //if no data to wrtie, start NextRead()
      void NextWrite() {
        int all_id = task_handler_->check_response();
        //LOG(INFO) << "Server writer: Checking response...buffer id is " << all_id;
        
        if (all_id == -1) { //no more response to process
          //NextRead();
          //UPDATE: should not come back with all_id = -1, just wait
           LOG(INFO) << "OHNO Server writer: check response returns -1";
        } else {
          int buffer_id = all_id / 4;
          int thread_id = all_id % 4;
          response_ = (MatrixResponse *)task_handler_->get_buffer_response(buffer_id, thread_id);
          StartWrite(&*(MatrixResponse *)response_);
          task_handler_->add_resource(thread_id);
          //LOG(INFO) << "Server writer: Has wrritten result from thread " << thread_id;
          //UPDATE
          //NextWrite();
        }
      }
        
      void NextRead() {
        current_buffer_ = task_handler_->select_next_buffer();
        if (current_buffer_ == -1) {
          std::cerr << "ERROR: No buffer available for receiving new request!" << std::endl;
        } else {
          //LOG(INFO) << "Select next buffer " << current_buffer_;
          int buffer_id = current_buffer_ / 4;
          int thread_id = current_buffer_ % 4;
          //request_ is the address of selected buffer
          request_ = (MatrixRequest *)task_handler_->get_buffer_request(buffer_id, thread_id);
          //LOG(INFO) << "IO reader: Current buffer is " << current_buffer_<< " Select next buffer "<< buffer_id << " of thread " << thread_id << ", start reading new task...";
          StartRead((MatrixRequest *)request_);
        } 
      }

      int current_buffer_;
      void* request_;
      void* response_;

      TaskHandler *task_handler_;
      std::mutex *mu_;

      std::thread writer_thread_;
      std::atomic<bool> stop_threads_{false};
      // absl::Mutex* mu_;
      // std::vector<MatrixRequest>* received_requests_ ABSL_GUARDED_BY(mu_);
    };
    return new ComputeRPC(task_handler_, &mu_);
  }

 private:
  TaskHandler* task_handler_; // generic TaskHandler

  // absl::Mutex request_mu_;
  std::mutex mu_;
  // TODO: can use lock free queue in <boost/lockfree/queue.hpp>
  std::queue<MatrixRequest> request_queue_;
};

void RunServer(const std::string& task_type, uint32_t task_size, const std::string& address) {
  
  std::string server_address(address);

  // create task handler based on task_type, ex:matrix
  // TaskHandler* handler = nullptr;
  std::unique_ptr<TaskHandler> handler;


  if (task_type == "matrix") {
    //handler = new MatrixClass(task_size);
    handler = std::make_unique<MatrixClass>(task_size);
  } else {
    std::cerr << "Unsupported task type: " << task_type << std::endl;
    return;
  }

//   DistMultImpl service(handler);
  DistMultImpl service(handler.get());

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  builder.SetMaxReceiveMessageSize(512 * 1024 * 1024);  // 64 MB, default is 4MB for incoming messages
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();

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


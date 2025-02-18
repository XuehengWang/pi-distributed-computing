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


#include "distmult_service.pb.h"
#include "distmult_service.grpc.pb.h"

//#include "function_map.h"
#include "task_handler.h"
#include "matrix_handler.h"
#include "compute_rpc.h"

using distmult::DistMultService;
using distmult::MatrixRequest;
using distmult::MatrixResponse;
using std::chrono::system_clock;

using matrixclass::MatrixClass;

std::atomic<bool> running(true); // Flag to control the server loop

class DistMultImpl final : public DistMultService::Service {
public:
    explicit DistMultImpl(TaskHandler* handler, const std::string& address, const std::string& port) : task_handler_(handler) {}

    ~DistMultImpl() {
      
    }
      std::cout << "Go Server Response: " << response <<std::endl;
 
     return new StartComputeRPCServer((char*)address, (char*)port, task_handler_, &mu_)// ComputeRPC(task_handler_, &mu_);
  }

 private:
  TaskHandler* task_handler_; // generic TaskHandler
  std::mutex mu_;
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

  //NEED TO KEEP THIS ALIVE AS WELL NOW!!!!!!!!!!!!!!!!i
    while (running)
    {
        
	   std::this_thread::sleep_for(std::chrono::seconds(1));//here I think the issue is....
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


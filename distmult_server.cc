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

#include "utils.h"
#include "resource_scheduler.h"

//using distmult::DistMultService;
using utils::MatrixRequest;
using utils::MatrixResponse;

using utils::matrix_t;
using utils::task_node_t;
using utils::FunctionID;
using utils::random_int;
using rpiresource::ResourceScheduler;
using utils::Submatrix;

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

class DistMultServer {
 public:
  DistMultServer(int port) {
    server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock_ == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock_, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    LOG(INFO) << "Server listening on port " << port;
  }

  void Start() {
    while (true) {
      sockaddr_in client_addr;
      socklen_t addr_len = sizeof(client_addr);
      int client_sock = accept(server_sock_, (struct sockaddr*)&client_addr, &addr_len);
      if (client_sock < 0) {
          perror("Accept failed");
          continue;
      }
      std::thread(&DistMultServer::HandleClient, this, client_sock).detach();
    }
  }

 private:
  void HandleClient(int client_sock) {
    while (true) {
      MatrixRequest request;
      ssize_t bytes_received = recv(client_sock, &request, sizeof(MatrixRequest), 0);
      if (bytes_received <= 0) {
          close(client_sock);
          return;
      }
      
      MatrixResponse response = ProcessRequest(request);
      send(client_sock, &response, sizeof(MatrixResponse), 0);
    }
  }

  MatrixResponse ProcessRequest(const MatrixRequest& request) {
    MatrixResponse response;
    // Implement matrix computation logic here
    LOG(INFO) << "Processing matrix request with task ID: " << request.task_id;
    return response;
  }

  int server_sock_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  DistMultServer server(PORT);
  server.Start();

  return EXIT_SUCCESS;
}


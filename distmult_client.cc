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
#include <future>
#include <unordered_map>
#include<boost/asio.hpp>
#include<boost/asio/ssl.hpp>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "task_handler.h"
#include "matrix_handler.h"
#include "utils.h"
#include "resource_scheduler.h"
#include "distmult_service.pb.h"

#define PORT 8080
#define CONNECTION_TIMEOUT 5

using boost::asio::ip::tcp;
using distmult::MatrixRequest;
using distmult::MatrixResponse;

using utils::matrix_t;
using utils::task_node_t;
using utils::FunctionID;
using utils::random_int;
using rpiresource::ResourceScheduler;
using utils::Submatrix;
/**The ComputeMatrix function waits for messages in the buffer(?) to send to the given secondary node, or it recieves data from a waiting secondary node)**/
class DistMultClient {
public:
	DistMultClient(boost::asio::io_context &io_context, const std::string &host, int port, std::queue<int>& result_queue, std::condition_variable& result_cv, std::mutex& result_lock,/** std::unordered_map<int, task_node_t*>& tasks
      , std::mutex& task_lock, std::condition_variable& task_cv, std::queue<int>& task_queue,**/ int submatrix_size )
		: socket_(io_context, ssl_context_), resolver_(io_context), result_queue_(result_queue), result_cv_(result_cv), result_lock_(result_lock),/** on_fly_tasks(tasks), task_queue_(task_queue), task_cv_(task_cv), 
          task_lock_(task_lock),**/ submatrix_size_(submatrix_size) {

		//	ssl_context_.set_verify_mode(boost::asio::ssl::verify_node);
			connect_to_server(host, port);
	}
	
	
	void initialize_request() {
	
		google::protobuf::RepeatedField<double>& inputa = *request_.mutable_inputa();
		google::protobuf::RepeatedField<double>& inputb = *request_.mutable_inputb();
		inputa.Resize(submatrix_size_*submatrix_size_, 0.0f);
		inputb.Resize(submatrix_size_*submatrix_size_, 0.0f);
		inputa_ptr_ = inputa.mutable_data();
		inputb_ptr_ = inputb.mutable_data();
		
	}
	void queue_message(/**Request request**/){
		/**create task here??**/
		int task_id;
		task_node_t *task;
		{
			std::unique_lock<std::mutex> lock(task_lock_);
			while (task_queue_.empty()){
			  task_cv_.wait(lock, [this]{return !task_queue_.empty() || done_;});
			}
			if(done_){
			Stop(); //define this function!
			return;
			}
			task_id = task_queue_.front();
			task_queue_.pop();
		}
		if (task_id == -1){
		  Stop(); //define this function!
		}
		auto it = on_fly_tasks.find(task_id); //define this list somewhere
		task = it->second;

		create_request(task->task_id, task->ops, task->n, task->left, task->right, task->left_matrix, task->right_matrix);
		LOG(INFO) << "[Client RPI " << task->assigned_rpi << "] Sending request " << request_.task_id() << " with ops " << task->ops << ", input A[0] = " << request_.inputa()[0];

		message_queue_.push(&request_);

		if(!sending_){
			send_next_message();
		}
	}
	
	
	//put the task into map for writer thread
	void add_task(task_node_t*& task) {
	    int task_id = task->task_id;
	    {
	      std::unique_lock<std::mutex> lock(task_lock_);
	      on_fly_tasks[task_id] = task;
	      task_queue_.push(task_id);
	    }
	    task_cv_.notify_one();
	    LOG(INFO) << "[ Client RPI " << task->assigned_rpi << " ]: add_task " << task->task_id;
	    queue_message();	    
	  }

	  void Stop() {
	    LOG(INFO) << "Client thread stop..."; 
	    {
	      std::unique_lock<std::mutex> lock(task_lock_);
	      task_queue_.push(-1);
	    }
	    task_cv_.notify_one();
	  }

private:
	boost::asio::ssl::context ssl_context_{boost::asio::ssl::context::tlsv12};
	boost::asio::ssl::stream<tcp::socket> socket_;
	tcp::resolver resolver_;
	std::queue<MatrixRequest*> message_queue_; //can we replace this with another queue
	std::vector<char> response_buffer_;//{1024}; //do we want this to be something else
	bool sending_ = false;

	void connect_to_server(const std::string &host, int port){
		resolver_.async_resolve(host, std::to_string(port), 
			[this](boost::system::error_code ec, tcp::resolver::results_type endpoints) { //what does this do?
				if (!ec) {
					boost::asio::async_connect(socket_.lowest_layer(), endpoints,
					[this](boost::system::error_code ec, const tcp::endpoint &) {
						if (!ec) {
							socket_.async_handshake(boost::asio::ssl::stream_base::client,
							[this](boost::system::error_code handshake_ec) {
								if (!handshake_ec) {
									std::cout << "Connected to server!" << std::endl;
									listen_for_server_responses();
								}
							});
						}
						});
					}
			});		
	}

	void send_next_message(){
		if (message_queue_.empty()){
			sending_ = false;
			return;			
		}

		sending_ = true;
		/** do we create a request here or do we use the variable within this class**/
		MatrixRequest* request = message_queue_.front();
		std::string serialized_request;
		request->SerializeToString(&serialized_request);

		boost::asio::async_write(socket_, boost::asio::buffer(serialized_request),
			[this](boost::system::error_code ec, std::size_t){
			   if(!ec){
				listen_for_server_responses();
			   }
			});
	}

	void create_request(int task_id, FunctionID ops, int n, Submatrix subA, Submatrix subB, matrix_t *inputA, matrix_t *inputB){
		request_.set_task_id(task_id);
		request_.set_ops(ops);
		request_.set_n(n);

		inputA->get_submatrix_data(subA, inputa_ptr_, inputA->data);
		inputB->get_submatrix_data(subB, inputb_ptr_, inputB->data);
	}

	void listen_for_server_responses() {
	    //int rpi_id;
	    //int task_id;
	    //int n; //where should these go if not here?
	    /**should this go before or after the response is read**/
	    socket_.async_read_some(boost::asio::buffer(response_buffer_), 
	    [this](boost::system::error_code ec, std::size_t length){
	      if(!ec) {
		//Response response;
		int rpi_id, task_id, n;
	        response_.ParseFromArray(response_buffer_.data(), length);
		task_node_t* task;
		{
		    task_id = response_.task_id();
		    n = response_.n();
		    auto it = on_fly_tasks.find(task_id);
		    task = it->second;
		    rpi_id = task->assigned_rpi;
		    on_fly_tasks.erase(task_id);
		  }

		   LOG(INFO) << "{CLient RPI " << rpi_id << " ] Received response for task_id: " << task_id
		  << " with n: " << n << std::endl;

		   {
			std::unique_lock<std::mutex> lock(result_lock_);
			result_queue_.push(rpi_id); //raspberry pi id
		   }
		   result_cv_.notify_one();
		   
		   //save output & send back task_id afterward
		   matrix_t *mat = new matrix_t(response_);
		   task->result = Submatrix(0, 0, n, n);
		   task -> result_matrix = mat;

		   {
			std::unique_lock<std::mutex> lock(result_lock_);
			result_queue_.push(task_id); //task_id
		   }
		   if(!message_queue_.empty()){
			  message_queue_.pop();
			  send_next_message();
		}
	      }
	    });
	}


/**Decide based upon the functions where these need to go..**/
      MatrixResponse response_;
      MatrixRequest request_;
      double* inputa_ptr_;
      double* inputb_ptr_;

      std::mutex& result_lock_;
      std::condition_variable& result_cv_;
      std::queue<int>& result_queue_;

      std::unordered_map<int, task_node_t*> on_fly_tasks;

      std::mutex task_lock_;
      std::condition_variable task_cv_;
      std::queue<int> task_queue_;

      // for overall status
      std::mutex mu_;
      std::condition_variable cv_;
      bool done_ = false;

      int submatrix_size_;
};


class ClusterManager {
public:
    ClusterManager(std::string &task_type, int num_rpi, std::vector<std::string>& addresses, int task_size, int subtask_size, int random_id_start): num_rpi_(0) {
      // LOG(INFO) << task_type;
      random_id_ = random_id_start;
      if (task_type == "matrix") {

        initialize_matrix_rpc(num_rpi, addresses);

        matrix_size_ = task_size;
        submatrix_size_ = subtask_size;
        std::this_thread::sleep_for(std::chrono::seconds(num_rpi * 5));
        //initialize_matrix_tasks(matrix_size_, submatrix_size_);

      } else {
        std::cerr << "Error: Unsupported task type \"" << task_type << "\". Supported: \"matrix\".\n";
      }

      //initialize_secondary_resources();

      //initialize_matrix_results();

      // start threads
      LOG(INFO) << "ClusterManager: Start reader and writer threads";
      //writer_thread_ = std::thread(&ClusterManager::writer, this);
      //reader_thread_ = std::thread(&ClusterManager::reader, this);

      auto now = std::chrono::high_resolution_clock::now();
      start_time = std::chrono::duration<double>(now.time_since_epoch()).count();
    }

  ~ClusterManager() {
    if (writer_thread_.joinable()) {
      writer_thread_.join();
      LOG(INFO) << "Writer joined!";
    }
    if (reader_thread_.joinable()) {
      reader_thread_.join();
      LOG(INFO) << "Reader joined!";
    }

    for (std::thread& t : client_threads_) {
        if (t.joinable()) {
            t.join();
        }
        LOG(INFO) << "All client joined!";
    }
    auto now = std::chrono::high_resolution_clock::now();
    double end = std::chrono::duration<double>(now.time_since_epoch()).count();

    double duration = end - start_time;
    std::cout << "Total time: " << duration << " seconds\n";

    client_map.clear();
  }

private:

    //initialize all DistMultClient for matrix task
    void initialize_matrix_rpc(int num_rpi_total, std::vector<std::string>& addresses) {

    	for (int i = 0; i < num_rpi_total; i++) {
		const auto& address = addresses[i];
		boost::asio::io_context io_context;
		std::thread sock_thread([&, address]() {
		LOG(INFO) << "Started Socket for this address " << address;
		  
		std::shared_ptr<DistMultClient> client = std::make_shared<DistMultClient>(io_context, address, 8080, result_queue_, result_cv_, result_lock_, submatrix_size_);
		//client_map[i] = client; //what is this i?
		LOG(INFO) << "Client Socket started for RPI_Id: " << i;
		//client->ComputeMatrix(i);
		//io_context.run();
		});
            	{
                	std::lock_guard<std::mutex> lock(thread_mutex_);
			client_threads_.emplace_back(std::move(sock_thread));
            	}

		num_rpi_++;
 
	}
    }	

    void initialize_matrix_tasks(int matrix_size, int submatrix_size) {
      whole_matrix_ = new matrix_t(matrix_size); //initialize matrix data in 2D format
      //whole_matrix_->print_matrix();
      create_tasks(matrix_size, submatrix_size, all_tasks_, initial_tasks_, whole_matrix_);
      //LOG(INFO) << "Created " << all_tasks_.size() << " in total, " << "starting with " << initial_tasks_.size() << " multiplications...";
      std::cout << "Created " << all_tasks_.size() << " in total, " << "starting with " << initial_tasks_.size() << " multiplications..." << std::endl;
      remaining_tasks_ = (matrix_size/submatrix_size) *  (matrix_size/submatrix_size) / 2;//subtrees
      //remaining_tasks_ = all_tasks_.size();
    }

    void initialize_secondary_resources(){
      for (int i = 0; i < num_rpi_; i++) {
        resource_scheduler.add_entry_head(i);
      }
      LOG(INFO) << "Initialized all RPIs' resources: ";
      resource_scheduler.printList();
    }


    void writer() {
      while (!stop_) {
        task_node_t* select_task;
        {
          std::unique_lock<std::mutex> lock(task_lock_);
          while (initial_tasks_.empty() && !stop_) {
            task_cv_.wait(lock, [this] { return !initial_tasks_.empty() || stop_; });
          }

          if (stop_) {
            break;
          }
          select_task = initial_tasks_.front();
          // LOG(INFO) << "selected task is " << select_task;
          initial_tasks_.erase(initial_tasks_.begin()); // TODO: pop()
        }

        // get resource (select secondary node)
        int select_rpi = resource_scheduler.consume_resource();
        auto it = client_map.find(select_rpi);
        if (it != client_map.end()) {
          std::shared_ptr<DistMultClient> client = it->second;
          // LOG(INFO) << "Found RPC Client with rpi_id " << select_rpi;

          // task_id + assigned_rpi
          select_task->assigned_rpi = select_rpi;

          //int task_id = random_int(1000,100000);
          int task_id = random_id_ + count_ % (num_rpi_ * 20);
          count_++;
          select_task->task_id = task_id;
          LOG(INFO) << "Writer: Selected task " << select_task << ", assigned to " << select_rpi << " with task_id = " << select_task->task_id;

          // int* buffer = new int[submatrix_size_ * submatrix_size_];
          // select_task->result = buffer;

          on_fly_tasks[task_id] = select_task;

          // // prepare the request?
          // LOG(INFO) << "Finish preparing the task " << task_id << " on Manager, sending to RPC Client...";
          // // notify the secondary to work for task
          client->add_task(select_task); //task_node_t
        } else {
          LOG(ERROR) << "Client with rpi_id " << select_rpi << " not found!" << std::endl;
          for (const auto& pair : client_map) {
              std::cout << pair.first << ": " << pair.second << std::endl;
          }

        }

      }
    }

    void reader() {
      while (!stop_) {
        int result_to_process;
        {
          std::unique_lock<std::mutex> lock(result_lock_);
          while (result_queue_.empty()) {
            result_cv_.wait(lock, [this] { return !result_queue_.empty();});
          }
          result_to_process = result_queue_.front();
          //result_queue_.erase(result_queue_.begin()); //TODO: optimize pop
          result_queue_.pop();
        }

        if (result_to_process <= num_rpi_) { //it's rpi_id, we will release resource
          resource_scheduler.produce_resource(result_to_process);
          continue;
        } else { //it's a task id, we will process real result
          process_matrix_result(result_to_process);

          // remaining_tasks_--;


          //LOG(INFO) << "Cluster: remaining tasks becomes " << remaining_tasks_;
          std::cout << "Finish: " << result_to_process << std::endl;

          //all tasks are done
          if (remaining_tasks_.load(std::memory_order_relaxed) == 0) {
            LOG(INFO) << "Reader: set stop to true";
            {
              std::unique_lock<std::mutex> lock(task_lock_);
              stop_ = true;
            }
            task_cv_.notify_one();
            clean_up();
          }
        }
      }
    }

    void process_matrix_result(int task_id) {
      LOG(INFO) << "Cluster: (processing result...) on_fly_tasks length is " << on_fly_tasks.size();
      std::cout << "Cluster: (processing result...) on_fly_tasks length is " << on_fly_tasks.size() << std::endl;
      auto it = on_fly_tasks.find(task_id);
      if (it != on_fly_tasks.end()) {
        task_node_t* task = it->second;
        LOG(INFO) << "Retrieved task with task_id " << task_id << ", processing result...";
        //on_fly_tasks.erase(task_id);

        // convert result to matrix_t
        // matrix_t mat(matrix_t(submatrix_size_, task->result));

        // matrix_t mat* = new matrix_t(submatrix_size_, task->result);
        // std::shared_ptr<matrix_t> mat = std::make_shared<matrix_t>(submatrix_size_, task->result);

        // position of the result matrix
        int subtree = task->subtree_id;
       LOG(INFO) << "Received result of subtree " << subtree;
        // get its parent
        task_node_t *parent = task->parent;
        if (!parent) { //root of subtree
          LOG(INFO) << "Subtree " << subtree << " completed!!";
          std::cout << "Subtree " << subtree << " completed!!" << std::endl;
          remaining_tasks_.fetch_sub(1, std::memory_order_relaxed);
          //remaining_tasks_--;
          LOG(INFO) << "Remaining tasks becomes " << remaining_tasks_;
          std::cout << "Remaining tasks becomes " << remaining_tasks_ << std::endl;
          //results_.push(std::move(mat));
          results_.push(task->result_matrix);

          on_fly_tasks.erase(task_id);
          return;
        }

        task_node_t *left_child_ptr = parent->left_child;
        task_node_t *right_child_ptr = parent->right_child;

        //if (left_child_ptr == task.get()) {

        bool push = false;
        {
          std::lock_guard<std::mutex> lock(parent_lock_);

          if (left_child_ptr == task) {
            std::cout << "This task is a left child of its parent :)" << std::endl;
            LOG(INFO) << "This task is a left child of its parent :)";
            parent->left = task->result;
            parent->left_matrix =task->result_matrix;
            parent->left.active = true;
          } else if (right_child_ptr == task) {
            std::cout << "This task is a right child of its parent :)" << std::endl;
            LOG(INFO) << "This task is a right child of its parent :)";
            parent->right = task->result;
            parent->right_matrix =task->result_matrix;
            parent->right.active = true;
          } else {
            std::cout << "What? Not a child of its parent :(" << std::endl;
            LOG(ERROR) << "What? Not a child of its parent :(";
          }
          if (parent->left.get_status() && parent->right.get_status()) {
            push = true;
          }
        }

        // check if parent can be added to the task queue
        if (push) {
          std::cout << "Done both children -> Add parent to the task queue!" << std::endl;
          LOG(INFO) << "Done both children -> Add parent to the task queue!";
          {
            std::unique_lock<std::mutex> lock(task_lock_);
            //initial_tasks_.insert(initial_tasks_.begin(), parent);
            initial_tasks_.push_back(parent);
            task_cv_.notify_one();
          }
        }

        //task->result_matrix->print_matrix();TODO
        all_tasks_.push_back(task); //TODO
        on_fly_tasks.erase(task_id);

      } else {
        std::cout << "Cannot find task_id: " << task_id << std::endl;
        LOG(INFO) << "Cannot find task_id: " << task_id;
      }
    }

    void clean_up() {
      LOG(INFO) << "In clean-up...";
      assert(initial_tasks_.empty());
      assert(on_fly_tasks.size() == 0);

      task_cv_.notify_all();
      // stop & close for all client rpc
      for (auto& [key, rpc] : client_map) {
        if (rpc) {
            LOG(INFO) << "RPC stop??";
            rpc->Stop();
        }
      }
    }

    int random_id_;
    int count_{0};
    int matrix_size_;
    int submatrix_size_;
    std::mutex parent_lock_;

    int num_rpi_;
    //int remaining_tasks_; //(1024/128)^2
    std::atomic<int> remaining_tasks_{0};
    matrix_t *whole_matrix_;
    //std::vector<std::shared_ptr<matrix_t>> results_;
    std::queue<matrix_t*> results_;


    /* Tasks */
    // store all tasks in FIFO order they created
    // so the tasks in the front of the queue should not depend on later tasks
    // nevermind, the task depending on others will not be added initially
    std::vector<task_node_t*> all_tasks_;
    std::vector<task_node_t*> initial_tasks_;

    std::unordered_map<int, task_node_t*> on_fly_tasks;

    /* Computation unit of each RPI */
    ResourceScheduler resource_scheduler;

    /* Map: rpi_id -> corresponding rpc client */
    std::unordered_map<int, std::shared_ptr<DistMultClient>> client_map;

    std::thread reader_thread_;
    std::thread writer_thread_;

    std::condition_variable task_cv_;
    std::mutex task_lock_;

    /* For RPC clients to send results */
    std::queue<int> result_queue_; //smaller numbers of rpi_id (< num_rpi), others are task_id (> 100 now)
    std::mutex result_lock_;
    std::condition_variable result_cv_;

    std::atomic<int> stop_{false};
    std::mutex thread_mutex_;
    std::vector<std::thread> client_threads_;
    //std::queue<matrix_t> intermediates_; // store intermediate results
    double start_time;
};

int main(int argc, char** argv) {
	absl::ParseCommandLine(argc, argv);
  	absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  	absl::InitializeLog();

	if (argc < 6) {
	    std::cerr << "Usage: " << argv[0] << " <task_type> <task_n> <subtask_n>  <task_id_start> <num_RPI> <address_1> ... <address_n>\n";
	    return EXIT_FAILURE;
	  }

	  std::string task_type = argv[1];
	  int num_RPI = std::stoi(argv[5]); // now we use fixed size of RPI workers
	  int task_size = std::stoi(argv[2]);
	  int subtask_size = std::stoi(argv[3]);
	  
	  int task_id_start = std::stoi(argv[4]);

	  if (argc != (6 + num_RPI)) {
	    std::cerr << "Error: Number of addresses provided does not match num_RPI.\n";
	    return EXIT_FAILURE;
	  }

	  std::vector<std::string> addresses;
	  for (int i = 0; i < num_RPI; ++i) {
	    std::string address = argv[6 + i];
	    addresses.push_back(address);
	    // manager.create_client(i, address);
	  }

	  ClusterManager manager(task_type, num_RPI, addresses, task_size, subtask_size, task_id_start);
	  return EXIT_SUCCESS;
}


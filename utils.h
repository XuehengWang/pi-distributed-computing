
#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include <random>

#include "distmult_service.pb.h"
#include "distmult_service.grpc.pb.h"
// #include "absl/flags/parse.h"
// #include "absl/log/globals.h"
// #include "absl/log/initialize.h"
// #include "absl/log/log.h"

namespace utils {
  
enum FunctionID { MULTIPLICATION, ADDITION };

class Submatrix {
public:
    size_t row_start;
    size_t col_start;
    size_t size;
    bool active;

    bool get_status() {
        return active;
    }

    Submatrix(size_t row, size_t col, size_t s)
        : row_start(row), col_start(col), size(s), active(false) {}
};

// 2D representation
struct matrix_t {
    int32_t** data;
    size_t n;  // matrix size (nxn)

    matrix_t(size_t size);
    
    matrix_t(size_t size, int* raw_data);//constructor using 1D array
    ~matrix_t(); 

    // Constructor that builds matrix from a MatrixResponse
    matrix_t(const distmult::MatrixResponse& msg) {
        const google::protobuf::RepeatedField<int32_t>& result = msg.result();
        size_t s = static_cast<size_t>(std::sqrt(result.size()));  // Assuming square matrix

        // Allocate memory for the matrix (2D array)
        data = new int32_t*[s];
        for (size_t i = 0; i < s; ++i) {
            data[i] = new int32_t[s];
        }

        // Populate the matrix from result
        size_t idx = 0;
        for (size_t i = 0; i < s; ++i) {
            for (size_t j = 0; j < s; ++j) {
                data[i][j] = result[idx++];
            }
        }

        //std::cout << "BUILD from response, n is " << s << std::endl;
        n = s;
    }

    // Move Constructor
    matrix_t(matrix_t&& other) noexcept : n(other.n), data(other.data) {
        other.data = nullptr;
        other.n = 0;
        std::cout << "matrix_t moved\n";
    }

    // Move Assignment Operator
    matrix_t& operator=(matrix_t&& other) noexcept {
        if (this != &other) {
            for (size_t i = 0; i < n; ++i) {
                delete[] data[i];
            }
            delete[] data;

            n = other.n;
            data = other.data;

            other.data = nullptr;
            other.n = 0;
        }
        std::cout << "matrix_t move-assigned\n";
        return *this;
    }

    //std::vector<int32_t>
    void get_submatrix_data(const Submatrix& submatrix, distmult::MatrixRequest& request, const std::string& pos) {
        //std::vector<int32_t> submatrix_data;
        if (pos == "inputa") {
            std::cout << "size " << submatrix.size << std::endl;
            for (size_t i = 0; i < submatrix.size; ++i) {
                for (size_t j = 0; j < submatrix.size; ++j) {
                    request.add_inputa(data[submatrix.row_start + i][submatrix.col_start + j]);
                }
            }
        } else {
            for (size_t i = 0; i < submatrix.size; ++i) {
                for (size_t j = 0; j < submatrix.size; ++j) {
                    request.add_inputb(data[submatrix.row_start + i][submatrix.col_start + j]);
                }
            }
        }
        
        //return submatrix_data;
    }

    

    // pointer to the top-left of a submatrix
    int32_t* get_submatrix(size_t row_start, size_t col_start, size_t submatrix_size);

    Submatrix track_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) {
        return Submatrix(row_start, col_start, submatrix_size);
    }

    // for 1D representation
    std::vector<int32_t> get_submatrix_data(size_t row_start, size_t col_start, size_t submatrix_size);

    void print_matrix() const;

    void print_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) const;
};


struct task_node_t {
    int32_t assigned_rpi;
    int32_t task_id;
    FunctionID ops;
    size_t n;
    //matrix_t* original_matrix;  // the large matrix
    // int32_t* left;    // Pointer to the submatrix
    // int32_t* right;
    Submatrix left;
    Submatrix right;
    matrix_t* left_matrix;
    matrix_t* right_matrix;
    matrix_t* result_matrix;
    int32_t subtree_id;
    // bool subtree_done;
    //int32_t *result; //TODO
    Submatrix result;
    task_node_t* parent;
    task_node_t* left_child;
    task_node_t* right_child;

    task_node_t(FunctionID ops, size_t n)
        : assigned_rpi(-1), ops(ops), n(n), parent(nullptr), left_child(nullptr), right_child(nullptr),
        left(0, 0, n), right(0, 0, n), result(0,0,n) {}
};

// struct subtree_t {
//   matrix_t submatrix;
//   size_t small_n; //n*n
//   std::vector<int> result;
// }

// // can be found in task_map
// struct task_result_t {
//   //if only this field, read thread to free up a resource; otherwise this field = -1
//   int rpi_id; 
//   int task_id;
//   // size_t n;
//   // int *result;
//   //std::vector<int> result;
// };
    
// Simulate sub tasks, for example: 1024 big matrix, 64*128*128 submatrices
int create_tasks(const size_t matrix_size, const size_t submatrix_size, std::vector<task_node_t*>& tasks_final, std::vector<task_node_t*>& tasks_init, matrix_t *mat);

int random_int(int min, int max);




} //namespace

#endif //UTILS_H
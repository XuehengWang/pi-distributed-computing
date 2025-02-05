
#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include <random>

#include "distmult_service.pb.h"

// #include "absl/flags/parse.h"
// #include "absl/log/globals.h"
// #include "absl/log/initialize.h"
// #include "absl/log/log.h"

namespace utils {
/**
class MatrixRequest {
public:
    int task_id;
    unsigned int ops;
    unsigned int n;
    std::vector<double> inputa;
    std::vector<double> inputb;

    // Default constructor
    MatrixRequest() : task_id(0), ops(0), n(0) {}
    
    MatrixRequest(int task_id, unsigned int ops, unsigned int n,
                  const std::vector<double>& inputa, const std::vector<double>& inputb)
        : task_id(task_id), ops(ops), n(n), inputa(inputa), inputb(inputb) {}
};

class MatrixResponse {
public:
    int task_id;
    unsigned int n;
    std::vector<double> result;
    
    //Default constructor
    MatrixResponse() : task_id(0), n(0) {}

    MatrixResponse(int task_id, unsigned int n, const std::vector<double>& result)
        : task_id(task_id), n(n), result(result) {}
    
    void print() const {
        std::cout << "Task ID: " << task_id << "\nResult: ";
        for (double val : result) {
            std::cout << val << " ";
        }
        std::cout << "\n";
    }
};
**/
enum FunctionID { MULTIPLICATION, ADDITION };

class Submatrix {
public:
    size_t row_start;
    size_t col_start;
    size_t size;
    bool active;
    size_t original_size;

    bool get_status() {
        return active;
    }

    Submatrix(size_t row, size_t col, size_t s, size_t parent_s)
        : row_start(row), col_start(col), size(s), active(false), original_size(parent_s) {}
};

// 2D representation
struct matrix_t {
    double* data;
    size_t n;  // matrix size (nxn)

    matrix_t(size_t size);
    
    matrix_t(size_t size, double* raw_data);//constructor using 1D array
    ~matrix_t(); 

    // Constructor that builds matrix from a MatrixResponse - we need to change this 
    matrix_t(const distmult::MatrixResponse& msg) {
	const google::protobuf::RepeatedField<double>& result = msg.result();
        //std::vector<double> result = msg.result;
        size_t s = static_cast<size_t>(std::sqrt(result.size()));  // square matrix

        // Allocate memory for the matrix (2D array)
        data = new double[s*s];
        for (size_t i = 0; i < s*s; ++i) {
            data[i] = result[i];
        }

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
            // for (size_t i = 0; i < n; ++i) {
            //     delete[] data[i];
            // }
            delete[] data;

            n = other.n;
            data = other.data;

            other.data = nullptr;
            other.n = 0;
        }
        std::cout << "matrix_t move-assigned\n";
        return *this;
    }

    //void get_submatrix_data(const Submatrix& submatrix, double *message, double *data, const std::string& pos) {
    void get_submatrix_data(const Submatrix& submatrix, double *message, double *data) {
        if (submatrix.row_start == 0 && submatrix.col_start == 0 && submatrix.size == submatrix.original_size) {
            std::copy(data, data + submatrix.size * submatrix.size, message);
        } else {
            for (int i = 0; i < submatrix.size; ++i) {
                int row_index = submatrix.row_start + i;
                int source_offset = row_index * submatrix.original_size + submatrix.col_start; 
                int destination_offset = i * submatrix.size; 
                std::copy(data + source_offset, data + source_offset + submatrix.size, message + destination_offset);
            }
        }
    }


//     // pointer to the top-left of a submatrix
//     double* get_submatrix(size_t row_start, size_t col_start, size_t submatrix_size);

    Submatrix track_submatrix(size_t row_start, size_t col_start, size_t submatrix_size, size_t matrix_size) {
        return Submatrix(row_start, col_start, submatrix_size, matrix_size);
    }

//     // for 1D representation
//     std::vector<double> get_submatrix_data(size_t row_start, size_t col_start, size_t submatrix_size);

     void print_matrix() const;

//     void print_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) const;
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

    task_node_t(FunctionID ops, size_t n, size_t parent_n)
        : assigned_rpi(-1), ops(ops), n(n), parent(nullptr), left_child(nullptr), right_child(nullptr),
        left(0, 0, n, parent_n), right(0, 0, n, parent_n), result(0, 0, n, parent_n) {}
};

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

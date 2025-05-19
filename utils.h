#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include <random>
#include <cmath>
#include <algorithm>

#include <capnp/message.h>
#include "matrix.capnp.h"

namespace utils {

enum FunctionID { MULTIPLICATION, ADDITION };

class Submatrix {
public:
    size_t row_start;
    size_t col_start;
    size_t size;
    bool active;
    size_t original_size;

    bool get_status() const {
        return active;
    }

    Submatrix(size_t row, size_t col, size_t s, size_t parent_s)
        : row_start(row), col_start(col), size(s), active(false), original_size(parent_s) {}
};

struct matrix_t {
    double* data;
    size_t n;  // matrix size (nxn)

    matrix_t(size_t size);

    matrix_t(size_t size, double* raw_data) : n(size) {
        data = new double[n * n];
        std::copy(raw_data, raw_data + n * n, data);
    }

    ~matrix_t();

    matrix_t(matrix_t&& other) noexcept : n(other.n), data(other.data) {
        other.data = nullptr;
        other.n = 0;
    }

    matrix_t& operator=(matrix_t&& other) noexcept {
        if (this != &other) {
            delete[] data;
            n = other.n;
            data = other.data;
            other.data = nullptr;
            other.n = 0;
        }
        return *this;
    }

    void print_matrix() const;

    void get_submatrix_data(const Submatrix& submatrix, double* message, const double* full_data) {
        if (submatrix.row_start == 0 && submatrix.col_start == 0 && submatrix.size == submatrix.original_size) {
            std::copy(full_data, full_data + submatrix.size * submatrix.size, message);
        } else {
            for (size_t i = 0; i < submatrix.size; ++i) {
                size_t row_index = submatrix.row_start + i;
                size_t source_offset = row_index * submatrix.original_size + submatrix.col_start;
                size_t destination_offset = i * submatrix.size;
                std::copy(full_data + source_offset, full_data + source_offset + submatrix.size, message + destination_offset);
            }
        }
    }

    Submatrix track_submatrix(size_t row_start, size_t col_start, size_t submatrix_size, size_t matrix_size) {
        return Submatrix(row_start, col_start, submatrix_size, matrix_size);
    }

    void loadFromCapnp(MatrixTask::Reader reader);
    void serializeToCapnp(MatrixResult::Builder builder) const;
    
};

struct task_node_t {
    int32_t assigned_rpi;
    int32_t task_id;
    FunctionID ops;
    size_t n;
    Submatrix left;
    Submatrix right;
    matrix_t* left_matrix;
    matrix_t* right_matrix;
    matrix_t* result_matrix;
    int32_t subtree_id;
    Submatrix result;
    task_node_t* parent;
    task_node_t* left_child;
    task_node_t* right_child;

    task_node_t(FunctionID ops, size_t n, size_t parent_n)
        : assigned_rpi(-1), task_id(-1), ops(ops), n(n),
          left(0, 0, n, parent_n), right(0, 0, n, parent_n), result(0, 0, n, parent_n),
          left_matrix(nullptr), right_matrix(nullptr), result_matrix(nullptr),
          parent(nullptr), left_child(nullptr), right_child(nullptr) {}
};

int create_tasks(const size_t matrix_size, const size_t submatrix_size,
                 std::vector<task_node_t*>& tasks_final,
                 std::vector<task_node_t*>& tasks_init,
                 matrix_t* mat);

int random_int(int min, int max);

} // namespace utils

#endif // UTILS_H

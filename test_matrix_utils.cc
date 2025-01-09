#include "utils.h"


using utils::matrix_t;
using utils::task_node_t;

// need to update this test...

void test_get_submatrix_data() {
    matrix_t mat(4); 

    std::vector<int32_t> submatrix_data = mat.get_submatrix_data(1, 1, 2);

    std::vector<int32_t> expected_data = {1, 1, 1, 1};  // Expected data for a 2x2 submatrix
    assert(submatrix_data == expected_data);
    std::cout << "get_submatrix_data() test passed!" << std::endl;

}


void test_matrix_initialization() {
    matrix_t mat(4);
    for (size_t i = 0; i < mat.n; ++i) {
        for (size_t j = 0; j < mat.n; ++j) {
            assert(mat.data[i][j] == 1); 
        }
    }
    std::cout << "Matrix initialization test passed!" << std::endl;
}

void test_matrix_initialization_from_1d_array() {
    size_t n = 16;
    int* raw_data = new int[n * n];

    for (size_t i = 0; i < n * n; ++i) {
        raw_data[i] = i + 1;  // 1, 2, ..., n*n
    }

    matrix_t mat(n, raw_data);

    for (size_t i = 0; i < mat.n; ++i) {
        for (size_t j = 0; j < mat.n; ++j) {
            int expected_value = raw_data[i * n + j];
            assert(mat.data[i][j] == expected_value);
        }
    }

    delete[] raw_data;

    std::cout << "Matrix initialization from 1D array test passed!" << std::endl;
}

void test_submatrix_extraction() {
    matrix_t mat(4);
    int* submatrix = mat.get_submatrix(1, 1, 2); 
    assert(submatrix == &mat.data[1][1]); 

    std::cout << "Submatrix extraction test passed!" << std::endl;
    // mat.print_matrix();
    // mat.print_submatrix(1, 1, 2);
}

void test_create_tasks() {
    std::vector<std::shared_ptr<task_node_t>> tasks;
    std::vector<std::shared_ptr<task_node_t>> tasks_start;
    matrix_t * mat = new matrix_t(4);
    create_tasks(4, 2, tasks, tasks_start, mat);

    assert(tasks.size() > 0);
    std::cout << "Task creation test passed!" << std::endl;
}


void test_large_matrix() {
    std::vector<std::shared_ptr<task_node_t>> tasks;
    std::vector<std::shared_ptr<task_node_t>> tasks_start;
    matrix_t * mat = new matrix_t(1024);
    create_tasks(1024, 128, tasks, tasks_start, mat);

    assert(tasks.size() > 0); 
    std::shared_ptr<utils::task_node_t> first = tasks[0];
    std::cout << *(first->left) << std::endl;
    std::cout << *(first->right) << std::endl;

    std::cout << "Large matrix task creation test passed!" << std::endl;
}

int main() {
    test_matrix_initialization();
    test_matrix_initialization_from_1d_array();
    test_submatrix_extraction();
    test_get_submatrix_data();  // Test for extracting submatrix data into a 1D vector
    test_create_tasks();
    test_large_matrix();  
    return 0;
}

//  std::vector<int32_t> left_data = mat.get_submatrix_data(row_start, col_start, submatrix_size);
//             std::vector<int32_t> right_data = mat.get_submatrix_data(row_start, col_start, submatrix_size);

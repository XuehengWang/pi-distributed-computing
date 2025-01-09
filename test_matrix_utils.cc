#include "utils.h"


using utils::matrix_t;
using utils::task_node_t;
// using utils::FunctionID;
// using utils::create_tasks;

// enum FunctionID { MULTIPLICATION, ADDITION };

// // 2D representation of the matrix
// struct matrix_t {
//     int32_t** data;
//     size_t n;  // matrix size (nxn)

//     matrix_t(size_t size) : n(size) {
//         data = new int32_t*[n];
//         for (size_t i = 0; i < n; ++i) {
//             data[i] = new int32_t[n];
//             for (size_t j = 0; j < n; ++j) {
//                 data[i][j] = 1;  // init to 1s
//             }
//         }
//     }

//     ~matrix_t() {
//         for (size_t i = 0; i < n; ++i) {
//             delete[] data[i];
//         }
//         delete[] data;
//     }

//     // Returns a pointer to the top-left corner of a submatrix (no data copy)
//     int32_t* get_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) {
//         //std::cout << "data here is " << data[row_start][col_start] << std::endl;
//         return &(data[row_start][col_start]);
//     }

//     std::vector<int32_t> get_submatrix_data(size_t row_start, size_t col_start, size_t submatrix_size) {
//         std::vector<int32_t> submatrix_data;
//         for (size_t i = 0; i < submatrix_size; ++i) {
//             for (size_t j = 0; j < submatrix_size; ++j) {
//                 submatrix_data.push_back(data[row_start + i][col_start + j]);
//             }
//         }
//         return submatrix_data;
//     }


//     // Prints the entire matrix
//     void print_matrix() const {
//         for (size_t i = 0; i < n; ++i) {
//             for (size_t j = 0; j < n; ++j) {
//                 std::cout << data[i][j] << " ";
//             }
//             std::cout << std::endl;
//         }
//     }


//     // Prints a submatrix (for debugging)
//     void print_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) const {
//         for (size_t i = row_start; i < row_start + submatrix_size; ++i) {
//             for (size_t j = col_start; j < col_start + submatrix_size; ++j) {
//                 std::cout << data[i][j] << " ";
//             }
//             std::cout << std::endl;
//         }
//     }
// };


// struct task_node_t {
//     int32_t assigned_worker;
//     int32_t task_id;
//     FunctionID ops;
//     matrix_t* original_matrix;  // Reference to the original matrix
//     int32_t* left;    // Pointer to the submatrix in the original matrix
//     int32_t* right;   // Pointer to the submatrix in the original matrix
//     std::unique_ptr<matrix_t> result;
//     task_node_t* parent;
//     task_node_t* left_child;
//     task_node_t* right_child;

//     task_node_t(FunctionID ops)
//         : assigned_worker(-1), ops(ops), parent(nullptr), left_child(nullptr), right_child(nullptr) {}
// };

// // Function to create tasks, referencing the original matrix (no copying)
// int create_tasks(const size_t matrix_size, const size_t submatrix_size, std::vector<std::shared_ptr<task_node_t>>& tasks_final, std::vector<std::shared_ptr<task_node_t>>& tasks_init) {
//     matrix_t mat(matrix_size);  // Large matrix

//     int sub_trees = (matrix_size / submatrix_size) * (matrix_size / submatrix_size);

//     // For each subtree (submatrix multiplication)   
//     for (size_t i = 0; i < sub_trees; ++i) {
//         std::vector<std::shared_ptr<task_node_t>> tasks;
//         int tasks_count = 0;
//         size_t num_multiplication_tasks = matrix_size / submatrix_size;

//         for (size_t j = 0; j < num_multiplication_tasks; ++j) {
//             size_t row_start = j * submatrix_size;
//             size_t col_start = size_t(i / (matrix_size / submatrix_size)) * submatrix_size;
//             //std::cout << "row_start: " << row_start << ", col_start: " << col_start << std::endl;
//             tasks_count++;
//             auto new_task = std::make_shared<task_node_t>(MULTIPLICATION);
//             new_task->original_matrix = &mat;  // Reference to the original matrix

        
//             new_task->left = mat.get_submatrix(row_start, col_start, submatrix_size);
//             new_task->right = mat.get_submatrix(col_start, row_start, submatrix_size);
//             tasks.push_back(std::move(new_task));  // Add task to the vector
//             tasks_init.push_back(std::move(new_task));
//         }

//         // Add addition tasks (combine results of multiplication tasks)
//         int dependency_counter = 0;
//         int base = matrix_size / submatrix_size / 2;
//         while (base >= 1) {
//             for (size_t j = 0; j < base; ++j) {
//                 tasks_count++;
//                 auto new_task = std::make_shared<task_node_t>(ADDITION);
//                 new_task->left = nullptr;  // Wait for result from multiplication task
//                 new_task->right = nullptr; // Wait for result from multiplication task

//                 tasks[dependency_counter]->parent = new_task.get();
//                 new_task->left_child = tasks[dependency_counter].get();
//                 dependency_counter++;

//                 tasks[dependency_counter]->parent = new_task.get();
//                 new_task->right_child = tasks[dependency_counter].get();
//                 dependency_counter++;

//                 tasks.push_back(std::move(new_task));  // Add addition task
//             }
//             base /= 2;
            
//         }
        
//         // std::cout << dependency_counter << ", " << tasks_count << std::endl;
//         assert(dependency_counter == tasks_count - 1);

//         // for (const auto& task : tasks) {
//         //     std::cout << "Task Type: ";
//         //     if (task->ops == MULTIPLICATION) {
//         //         std::cout << "MULTIPLICATION\n";
//         //     } else {
//         //         std::cout << "ADDITION\n";
//         //     }

//         // }
//         tasks_final.insert(tasks_final.end(), tasks.begin(), tasks.end());

//     }
//     std::cout << "Total tasks = " <<  tasks_final.size() << std::endl;
//     std::cout << "Initial tasks = " <<  tasks_init.size() << std::endl;
//     return 0;
// }

void test_get_submatrix_data() {
    matrix_t mat(4);  // Initialize a 4x4 matrix with all elements set to 1

    // Get 2x2 submatrix starting from (1,1)
    std::vector<int32_t> submatrix_data = mat.get_submatrix_data(1, 1, 2);
    
    // Verify the data of the submatrix is correct
    std::vector<int32_t> expected_data = {1, 1, 1, 1};  // Expected data for a 2x2 submatrix
    assert(submatrix_data == expected_data);

    // Print the result of the test
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
        raw_data[i] = i + 1;  // initializing with values 1, 2, ..., n*n
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

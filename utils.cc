#include "utils.h"

namespace utils{


matrix_t::matrix_t(size_t size) : n(size) {
    data = new double[n*n];
    for (size_t i = 0; i < n*n; ++i) {
        data[i] = 1;
        // for (size_t j = 0; j < n; ++j) {
        //     data[i][j] = 2;  // init to 1s
        // }
    }
}

// // initialize using a 1D array
// matrix_t::matrix_t(size_t size, double* raw_data) : n(size) {
//     data = new double*[n];
//     for (size_t i = 0; i < n; ++i) {
//         data[i] = new double[n];
//         for (size_t j = 0; j < n; ++j) {
//             std::cout << raw_data[i * n + j] << " ";
//             data[i][j] = raw_data[i * n + j];  

//         }
//     }
// }

matrix_t::~matrix_t() {
    // for (size_t i = 0; i < n; ++i) {
    //     delete[] data[i];
    // }
    delete[] data;
}


// // pointer to the top-left of a submatrix
// double* matrix_t::get_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) {
//     //std::cout << "data here is " << data[row_start][col_start] << std::endl;
//     return &(data[row_start][col_start]);
// }

// // for 1D representation
// std::vector<double> matrix_t::get_submatrix_data(size_t row_start, size_t col_start, size_t submatrix_size) {
//     std::vector<double> submatrix_data;
//     for (size_t i = 0; i < submatrix_size; ++i) {
//         for (size_t j = 0; j < submatrix_size; ++j) {
//             submatrix_data.push_back(data[row_start + i][col_start + j]);
//         }
//     }
//     return submatrix_data;
// }


void matrix_t::print_matrix() const {
    std::cout << "In printing... n = " << n << std::endl;
    
    for (size_t i = 0; i < n*n; ++i) {
        std::cout << data[i] << " ";
        if (i != 0 && !((i+1) % n)) {
            std::cout << std::endl;
        }
    }
    std::cout << std::endl;
}

// void matrix_t::print_submatrix(size_t row_start, size_t col_start, size_t submatrix_size) const {
//     for (size_t i = row_start; i < row_start + submatrix_size; ++i) {
//         for (size_t j = col_start; j < col_start + submatrix_size; ++j) {
//             std::cout << data[i][j] << " ";
//         }
//         std::cout << std::endl;
//     }
// }

int create_tasks(const size_t matrix_size, const size_t submatrix_size, std::vector<task_node_t*>& tasks_final, std::vector<task_node_t*>& tasks_init, matrix_t *mat) {

    int sub_trees = (matrix_size / submatrix_size) * (matrix_size / submatrix_size)/2;

    // for each subtree (submatrix multiplication)   
    for (size_t i = 0; i < sub_trees; ++i) {
        std::vector<task_node_t*> tasks;
        int tasks_count = 0;

        // each subtree has 1024/128 multiplication tasks 
        size_t num_multiplication_tasks = matrix_size / submatrix_size;

        // Simulate submatrices using original matrix and pointers
        for (size_t j = 0; j < num_multiplication_tasks; ++j) {
            
            size_t col_start = j * submatrix_size;
            size_t row_start = size_t(i / (matrix_size / submatrix_size)) * submatrix_size;
            std::cout << "row_start: " << row_start << ", col_start: " << col_start << std::endl;
            tasks_count++;
            //auto new_task = std::make_shared<task_node_t>(MULTIPLICATION, submatrix_size);
            task_node_t* new_task = new task_node_t(MULTIPLICATION, submatrix_size, matrix_size);
            //new_task->original_matrix = mat;  // Reference to the original matrix
            new_task->left_matrix = mat;
            new_task->right_matrix = mat;
            new_task->result_matrix = nullptr;


            // use original mat?
            // new_task->left = mat->get_submatrix(row_start, col_start, submatrix_size);
            // new_task->right = mat->get_submatrix(col_start, row_start, submatrix_size);

            new_task->left = mat->track_submatrix(row_start, col_start, submatrix_size, matrix_size);
            new_task->right = mat->track_submatrix(col_start, row_start, submatrix_size, matrix_size);
            new_task->subtree_id = i;
            // new_task->subtree_done = false;
            // tasks.push_back(std::move(new_task));  // Add task to the vector
            // tasks_init.push_back(std::move(new_task));
            // UPDATE:
            tasks.push_back(new_task); 
            tasks_init.push_back(new_task);  // shared_ptr, do not move ownership

        }

        // each pair of results does addition
        int dependency_counter = 0;
        int base = matrix_size / submatrix_size / 2;
        while (base >= 1) {
            for (size_t j = 0; j < base; ++j) {
                tasks_count++;
                task_node_t* new_task = new task_node_t(MULTIPLICATION, submatrix_size, matrix_size);
                //auto new_task = std::make_shared<task_node_t>(ADDITION, submatrix_size);
                // new_task->left = nullptr;  // Wait for result
                
                // tasks[dependency_counter]->parent = new_task.get();
                // new_task->left_child = tasks[dependency_counter].get();
                tasks[dependency_counter]->parent = new_task;
                new_task->left_child = tasks[dependency_counter];
                dependency_counter++;

                tasks[dependency_counter]->parent = new_task;
                new_task->right_child = tasks[dependency_counter];
                dependency_counter++;
                // add addition tasks
                new_task->subtree_id = i;
                // if (base == 1) { //last addition task in this subtree
                //     new_task->subtree_done = true;
                // } else {
                //     new_task->subtree_done = false;
                // }
                //tasks.push_back(std::move(new_task));
                tasks.push_back(new_task);
            }
            base /= 2;
        }
       
        std::cout << dependency_counter << ", " << tasks_count << std::endl;
        assert((dependency_counter == tasks.size() - 1)); // power of 2

        // for (const auto& task : tasks) {
        //     std::cout << "Task Type: ";
        //     if (task->ops == MULTIPLICATION) {
        //         std::cout << "MULTIPLICATION\n";
        //     } else {
        //         std::cout << "ADDITION\n";
        //     }

        // }
        
        tasks_final.insert(tasks_final.end(), tasks.begin(), tasks.end());
    }
    std::cout << "Total tasks = " <<  tasks_final.size() << std::endl;
    std::cout << "Initial tasks = " <<  tasks_init.size() << std::endl;

    return 0;
}

int random_int(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

}


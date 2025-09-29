%{
#include "ImageLocalization.h"
%}

// Add typemaps for std::vector<std::vector<double>>
%include "std_vector.i"
%template(VectorDouble) std::vector<double>;
%template(VectorVectorDouble) std::vector<std::vector<double>>;

%extend localization{
    // Python-friendly wrapper for fit2DGaussian that accepts nested lists
    static int fit2DGaussian_array(std::vector<double> vars, PyObject* input) {
        // Convert Python nested list to std::vector<std::vector<double>>
        if (!PyList_Check(input)) {
            PyErr_SetString(PyExc_ValueError, "Expected a list of lists");
            return -1;
        }
        
        int rows = PyList_Size(input);
        if (rows == 0) {
            PyErr_SetString(PyExc_ValueError, "Empty input array");
            return -1;
        }
        
        std::vector<std::vector<double>> cpp_data;
        cpp_data.reserve(rows);
        
        for (int i = 0; i < rows; i++) {
            PyObject* row = PyList_GetItem(input, i);
            if (!PyList_Check(row)) {
                PyErr_SetString(PyExc_ValueError, "Expected a list of lists");
                return -1;
            }
            
            int cols = PyList_Size(row);
            std::vector<double> cpp_row;
            cpp_row.reserve(cols);
            
            for (int j = 0; j < cols; j++) {
                PyObject* item = PyList_GetItem(row, j);
                double value = PyFloat_AsDouble(item);
                if (PyErr_Occurred()) {
                    return -1;
                }
                cpp_row.push_back(value);
            }
            cpp_data.push_back(cpp_row);
        }
        
        // Call the original method
        return localization::fit2DGaussian(vars, cpp_data);
    }

    // NumPy-friendly overload using existing global typemaps from misc_types.i
    // Accepts a contiguous 2D double array (numpy) via (double* input, int n_input1, int n_input2)
    static int fit2DGaussian_numpy(std::vector<double> vars, double *input, int n_input1, int n_input2) {
        // Build a view into the flat buffer without Python APIs
        const int rows = n_input1;
        const int cols = n_input2;
        if (rows <= 0 || cols <= 0) {
            return -1;
        }
        std::vector<std::vector<double>> cpp_data(rows, std::vector<double>(cols));
        for (int i = 0; i < rows; ++i) {
            const double *src = input + static_cast<size_t>(i) * static_cast<size_t>(cols);
            std::copy(src, src + cols, cpp_data[i].begin());
        }
        return localization::fit2DGaussian(vars, cpp_data);
    }

    // Python-friendly wrapper for model2DGaussian that returns nested lists
    static PyObject* model2DGaussian_array(std::vector<double> vars, int rows, int cols) {
        // Create std::vector<std::vector<double>> with specified dimensions
        std::vector<std::vector<double>> cpp_data(rows, std::vector<double>(cols, 0.0));
        
        // Call the original method
        localization::model2DGaussian(vars, cpp_data);
        
        // Convert result to Python nested list
        PyObject* result = PyList_New(rows);
        for (int i = 0; i < rows; i++) {
            PyObject* row = PyList_New(cols);
            for (int j = 0; j < cols; j++) {
                PyList_SetItem(row, j, PyFloat_FromDouble(cpp_data[i][j]));
            }
            PyList_SetItem(result, i, row);
        }
        
        return result;
    }

    // NumPy-friendly output (matches CLSMImage::get_intensity style)
    // Typemaps from misc_types.i:
    //   %apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2){(double** output, int* dim1, int* dim2)}
    static void model2DGaussian_numpy(std::vector<double> vars,
                                      int rows,
                                      int cols,
                                      double **output,
                                      int *dim1,
                                      int *dim2) {
        if (rows <= 0 || cols <= 0) {
            *output = nullptr;
            *dim1 = *dim2 = 0;
            return;
        }
        // produce model in temporary 2D vector
        std::vector<std::vector<double>> cpp_data(rows, std::vector<double>(cols, 0.0));
        localization::model2DGaussian(vars, cpp_data);

        // allocate contiguous buffer expected by ARGOUTVIEWM_ARRAY2
        size_t n = static_cast<size_t>(rows) * static_cast<size_t>(cols);
        double *buf = (double*) malloc(n * sizeof(double));
        for (int i = 0; i < rows; ++i) {
            std::copy(cpp_data[i].begin(), cpp_data[i].end(), buf + static_cast<size_t>(i) * cols);
        }
        *output = buf;
        *dim1 = rows;
        *dim2 = cols;
    }
}

%include "ImageLocalization.h"

%{
#include "include/MicrotimeLinearization.h"
%}

// Exception handling for std::invalid_argument
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}

// Note: Vector and map templates are already defined in misc_types.i
// Do not redefine them here to avoid SWIG duplicate symbol errors

%include "include/MicrotimeLinearization.h"

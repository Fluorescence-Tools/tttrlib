#ifndef TTTRLIB_INFO_H
#define TTTRLIB_INFO_H

#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _OPENMP
#include <omp.h>
#endif

#define RECORD_PHOTON               0
#define RECORD_MARKER               1

// Maximum number of routing channels (can be overridden via CMake)
#ifndef TTTRLIB_MAX_ROUTING_CHANNELS
#define TTTRLIB_MAX_ROUTING_CHANNELS 256
#endif

// CPUID for runtime CPU feature detection (x86/x64 only)
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
    #if defined(__GNUC__) || defined(__clang__)
        #include <cpuid.h>
    #elif defined(_MSC_VER)
        #include <intrin.h>
    #endif
    #define TTTRLIB_X86_FEATURES 1
#else
    #define TTTRLIB_X86_FEATURES 0
#endif

// Runtime CPU feature detection
namespace tttrlib {
namespace cpu_features {
    
    // Detect CPU features at runtime using CPUID
    inline void detect_features(bool& has_avx, bool& has_fma) {
        has_avx = false;
        has_fma = false;
        
#if TTTRLIB_X86_FEATURES
    #if defined(_MSC_VER)
        // MSVC
        int cpu_info[4];
        __cpuid(cpu_info, 0);
        int n_ids = cpu_info[0];
        
        if (n_ids >= 1) {
            __cpuidex(cpu_info, 1, 0);
            has_avx = (cpu_info[2] & (1 << 28)) != 0;  // ECX bit 28
            has_fma = (cpu_info[2] & (1 << 12)) != 0;  // ECX bit 12
        }
    #elif defined(__GNUC__) || defined(__clang__)
        // GCC/Clang
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            has_avx = (ecx & bit_AVX) != 0;
            has_fma = (ecx & bit_FMA) != 0;
        }
    #endif
#endif
        // On non-x86 architectures (e.g., ARM), AVX/FMA are not available
    }
    
    // Cross-platform helper to safely get environment variable
    inline const char* safe_getenv(const char* env_var_name, char* buffer, size_t buffer_size) {
#ifdef _WIN32
        size_t required_size = 0;
        errno_t err = getenv_s(&required_size, buffer, buffer_size, env_var_name);
        return (err == 0 && required_size > 0) ? buffer : nullptr;
#else
        (void)buffer;       // Unused on non-Windows
        (void)buffer_size;  // Unused on non-Windows
        return std::getenv(env_var_name);
#endif
    }
    
    // Helper to check if string represents a false value
    inline bool is_false_value(const char* value) {
        return std::strcmp(value, "0") == 0 || 
               std::strcmp(value, "false") == 0 || 
               std::strcmp(value, "FALSE") == 0 ||
               std::strcmp(value, "off") == 0 ||
               std::strcmp(value, "OFF") == 0;
    }
    
    // Helper to check environment variable for feature override
    inline bool is_feature_enabled_by_env(const char* env_var_name, bool default_value) {
        char env_buffer[256];
        const char* env_val = safe_getenv(env_var_name, env_buffer, sizeof(env_buffer));
        
        if (env_val != nullptr) {
            return !is_false_value(env_val);
        }
        return default_value;
    }
    
    // Get AVX status (CPU detection + environment override)
    inline bool get_avx_enabled() {
#if defined(TTTRLIB_WITH_AVX)
        constexpr bool avx_compiled_in = (TTTRLIB_WITH_AVX != 0);
#else
        constexpr bool avx_compiled_in = true;
#endif

        if (!avx_compiled_in) {
            return false;
        }

        bool has_avx = false;
        bool has_fma = false;
        detect_features(has_avx, has_fma);
        return is_feature_enabled_by_env("TTTRLIB_USE_AVX", has_avx);
    }
    
    // Get FMA status (CPU detection + environment override)
    inline bool get_fma_enabled() {
        bool has_avx = false;
        bool has_fma = false;
        detect_features(has_avx, has_fma);
        
        // FMA requires AVX
        if (!get_avx_enabled()) return false;
        
        return is_feature_enabled_by_env("TTTRLIB_USE_FMA", has_fma);
    }
    
    // Get OpenMP status (compile-time + environment override)
    inline bool get_openmp_enabled() {
#ifdef _OPENMP
        // OpenMP available at compile time
        bool default_enabled = true;
#else
        // OpenMP not available at compile time
        bool default_enabled = false;
#endif
        return is_feature_enabled_by_env("TTTRLIB_USE_OPENMP", default_enabled);
    }
    
    // Get number of OpenMP threads (respects OMP_NUM_THREADS and TTTRLIB_NUM_THREADS)
    inline int get_openmp_num_threads() {
#ifdef _OPENMP
        char env_buffer[32];
        
        // Check TTTRLIB-specific override first
        const char* tttr_threads = safe_getenv("TTTRLIB_NUM_THREADS", env_buffer, sizeof(env_buffer));
        if (tttr_threads != nullptr) {
            int n = std::atoi(tttr_threads);
            if (n > 0) return n;
        }
        
        // Fall back to OMP_NUM_THREADS or default
        const char* omp_threads = safe_getenv("OMP_NUM_THREADS", env_buffer, sizeof(env_buffer));
        if (omp_threads != nullptr) {
            int n = std::atoi(omp_threads);
            if (n > 0) return n;
        }
        
        // Default: use all available threads
        return 0; // 0 means use OpenMP default
#else
        return 1; // Single-threaded if OpenMP not available
#endif
    }
    
    // Configure OpenMP threads and optionally log settings
    // Returns: actual number of threads that will be used
    inline int configure_openmp(bool verbose = false) {
#ifdef _OPENMP
        bool use_openmp = get_openmp_enabled();
        int num_threads = get_openmp_num_threads();
        
        if (use_openmp && num_threads > 0) {
            omp_set_num_threads(num_threads);
        }
        
        if (verbose) {
            std::clog << "-- Parallel processing enabled: " << (use_openmp ? "yes" : "no") << std::endl;
            if (use_openmp) {
                int actual_threads = (num_threads > 0) ? num_threads : omp_get_max_threads();
                std::clog << "-- OpenMP threads: " << actual_threads << std::endl;
            }
        }
        
        return use_openmp ? ((num_threads > 0) ? num_threads : omp_get_max_threads()) : 1;
#else
        if (verbose) {
            std::clog << "-- Parallel processing enabled: no (OpenMP not available)" << std::endl;
        }
        return 1;
#endif
    }
    
} // namespace cpu_features

namespace env {

    // Returns whether macro-time compression should be enabled when reading
    inline bool compress_on_read_enabled() {
        char env_buffer[256];
        const char* env_val = cpu_features::safe_getenv("TTTR_COMPRESS_ON_READ", env_buffer, sizeof(env_buffer));
        if (env_val != nullptr) {
            return !cpu_features::is_false_value(env_val);
        }
        return true;
    }

    // Initialize auto_compress_on_read with logging
    // This is used as a static initializer in TTTR class
    inline bool init_auto_compress_on_read() {
        bool enabled = compress_on_read_enabled();
        // Note: Logging would require including Verbose.h, which creates circular dependency
        // So logging is handled in TTTR.cpp instead
        return enabled;
    }

} // namespace env

} // namespace tttrlib

#endif //TTTRLIB_INFO_H

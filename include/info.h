#ifndef TTTRLIB_INFO_H
#define TTTRLIB_INFO_H

#include <cstdlib>
#include <cstring>

#define RECORD_PHOTON               0
#define RECORD_MARKER               1

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
    
    // Helper to check environment variable for feature override
    inline bool is_feature_enabled_by_env(const char* env_var_name, bool default_value) {
        const char* env_val = std::getenv(env_var_name);
        if (env_val != nullptr) {
            // Check if explicitly disabled
            if (std::strcmp(env_val, "0") == 0 || 
                std::strcmp(env_val, "false") == 0 || 
                std::strcmp(env_val, "FALSE") == 0 ||
                std::strcmp(env_val, "off") == 0 ||
                std::strcmp(env_val, "OFF") == 0) {
                return false;
            }
            // Any other value means enabled
            return true;
        }
        return default_value;
    }
    
    // Get AVX status (CPU detection + environment override)
    inline bool get_avx_enabled() {
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
        // Check TTTRLIB-specific override first
        const char* tttr_threads = std::getenv("TTTRLIB_NUM_THREADS");
        if (tttr_threads != nullptr) {
            int n = std::atoi(tttr_threads);
            if (n > 0) return n;
        }
        
        // Fall back to OMP_NUM_THREADS or default
        const char* omp_threads = std::getenv("OMP_NUM_THREADS");
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
    
} // namespace cpu_features
} // namespace tttrlib

#endif //TTTRLIB_INFO_H

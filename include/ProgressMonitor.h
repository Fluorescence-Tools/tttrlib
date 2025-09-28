#ifndef TTTRLIB_PROGRESSMONITOR_H
#define TTTRLIB_PROGRESSMONITOR_H

#include <iostream>
#include <chrono>
#include <string>

/**
 * @brief A reusable progress monitoring class for long-running operations
 * 
 * This class provides efficient progress reporting without the overhead of 
 * printing every iteration. It uses configurable update intervals and 
 * provides both percentage and rate information.
 */
class ProgressMonitor {
private:
    size_t total_items;           ///< Total number of items to process
    size_t current_count;         ///< Current number of processed items
    size_t update_interval;       ///< How often to update (every N items)
    size_t last_update_count;     ///< Count at last update
    std::chrono::steady_clock::time_point start_time;   ///< When monitoring started
    std::chrono::steady_clock::time_point last_update_time;  ///< Last update time
    std::string operation_name;   ///< Name of the operation being monitored
    bool enabled;                 ///< Whether progress monitoring is enabled
    
public:
    /**
     * @brief Constructor
     * @param total Total number of items to process
     * @param operation_name Name of the operation (e.g., "Filling pixels")
     * @param update_interval How often to print updates (default: every 1% or 10000 items, whichever is smaller)
     * @param enabled Whether to enable progress monitoring (default: true)
     */
    ProgressMonitor(size_t total, 
                   const std::string& operation_name = "Processing", 
                   size_t update_interval = 0,
                   bool enabled = true);
    
    /**
     * @brief Start monitoring (resets counters and timer)
     */
    void start();
    
    /**
     * @brief Update progress by one item
     * @return true if progress was printed, false otherwise
     */
    bool update();
    
    /**
     * @brief Update progress by N items
     * @param count Number of items processed
     * @return true if progress was printed, false otherwise
     */
    bool update(size_t count);
    
    /**
     * @brief Force print current progress regardless of interval
     */
    void force_update();
    
    /**
     * @brief Finish monitoring and print final statistics
     */
    void finish();
    
    /**
     * @brief Get current progress percentage
     * @return Progress as percentage (0.0 to 100.0)
     */
    double get_percentage() const;
    
    /**
     * @brief Get processing rate (items per second)
     * @return Items processed per second
     */
    double get_rate() const;
    
    /**
     * @brief Get estimated time remaining in seconds
     * @return Estimated seconds remaining, or -1 if cannot estimate
     */
    double get_eta_seconds() const;
    
    /**
     * @brief Check if monitoring is enabled
     * @return true if enabled
     */
    bool is_enabled() const { return enabled; }
    
    /**
     * @brief Enable or disable monitoring
     * @param enable true to enable, false to disable
     */
    void set_enabled(bool enable) { enabled = enable; }
    
private:
    /**
     * @brief Print current progress information
     */
    void print_progress();
    
    /**
     * @brief Format time duration as human-readable string
     * @param seconds Duration in seconds
     * @return Formatted string (e.g., "2m 30s")
     */
    std::string format_duration(double seconds) const;
};

#endif // TTTRLIB_PROGRESSMONITOR_H

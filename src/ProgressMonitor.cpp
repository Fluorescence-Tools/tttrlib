#include "ProgressMonitor.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

ProgressMonitor::ProgressMonitor(size_t total, 
                                const std::string& operation_name, 
                                size_t update_interval,
                                bool enabled)
    : total_items(total)
    , current_count(0)
    , last_update_count(0)
    , operation_name(operation_name)
    , enabled(enabled)
{
    // If no update interval specified, use 1% of total or 10000, whichever is smaller
    if (update_interval == 0) {
        this->update_interval = std::min(total / 100, static_cast<size_t>(10000));
        // Ensure minimum interval of 1
        if (this->update_interval == 0) {
            this->update_interval = 1;
        }
    } else {
        this->update_interval = update_interval;
    }
}

void ProgressMonitor::start() {
    current_count = 0;
    last_update_count = 0;
    start_time = std::chrono::steady_clock::now();
    last_update_time = start_time;
    
    if (enabled) {
        std::clog << "-- " << operation_name << ": Starting processing of " 
                  << total_items << " items..." << std::endl;
    }
}

bool ProgressMonitor::update() {
    return update(1);
}

bool ProgressMonitor::update(size_t count) {
    if (!enabled) return false;
    
    current_count += count;
    
    // Check if we should print an update
    if (current_count - last_update_count >= update_interval || current_count >= total_items) {
        print_progress();
        last_update_count = current_count;
        last_update_time = std::chrono::steady_clock::now();
        return true;
    }
    
    return false;
}

void ProgressMonitor::force_update() {
    if (!enabled) return;
    
    print_progress();
    last_update_count = current_count;
    last_update_time = std::chrono::steady_clock::now();
}

void ProgressMonitor::finish() {
    if (!enabled) return;
    
    current_count = total_items;  // Ensure we show 100%
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::clog << "-- " << operation_name << ": Completed 100.0% (" 
              << total_items << "/" << total_items << ") in " 
              << format_duration(duration.count() / 1000.0);
    
    if (duration.count() > 0) {
        double rate = static_cast<double>(total_items) / (duration.count() / 1000.0);
        std::clog << " [" << std::fixed << std::setprecision(1) << rate << " items/s]";
    }
    
    std::clog << std::endl;
}

double ProgressMonitor::get_percentage() const {
    if (total_items == 0) return 100.0;
    return (static_cast<double>(current_count) / total_items) * 100.0;
}

double ProgressMonitor::get_rate() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    
    if (elapsed.count() <= 0) return 0.0;
    
    return static_cast<double>(current_count) / (elapsed.count() / 1000.0);
}

double ProgressMonitor::get_eta_seconds() const {
    if (current_count == 0 || current_count >= total_items) return -1.0;
    
    double rate = get_rate();
    if (rate <= 0.0) return -1.0;
    
    size_t remaining = total_items - current_count;
    return static_cast<double>(remaining) / rate;
}

void ProgressMonitor::print_progress() {
    double percentage = get_percentage();
    double rate = get_rate();
    
    std::clog << "-- " << operation_name << ": " 
              << std::fixed << std::setprecision(1) << percentage << "% ("
              << current_count << "/" << total_items << ")";
    
    if (rate > 0.0) {
        std::clog << " [" << std::fixed << std::setprecision(1) << rate << " items/s]";
        
        double eta = get_eta_seconds();
        if (eta > 0.0) {
            std::clog << " ETA: " << format_duration(eta);
        }
    }
    
    std::clog << std::endl;
}

std::string ProgressMonitor::format_duration(double seconds) const {
    if (seconds < 0) return "unknown";
    
    std::ostringstream oss;
    
    if (seconds < 60) {
        oss << std::fixed << std::setprecision(1) << seconds << "s";
    } else if (seconds < 3600) {
        int minutes = static_cast<int>(seconds / 60);
        int secs = static_cast<int>(seconds) % 60;
        oss << minutes << "m " << secs << "s";
    } else {
        int hours = static_cast<int>(seconds / 3600);
        int minutes = static_cast<int>((seconds - hours * 3600) / 60);
        oss << hours << "h " << minutes << "m";
    }
    
    return oss.str();
}

#include "include/LayerNode.h"
#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "include/CLSMImage.h"
#include <algorithm>
#include <numeric>
#include <cmath>

// ===== LayerNode Implementation =====

LayerNode::LayerNode(uint32_t operation, GraphNode* parent)
    : GraphNode(parent) {
    properties_.raw = 0;  // Initialize all bits to 0
    properties_.operation = operation;
    properties_.split_strategy = SPLIT_BY_MARKERS;
    properties_.combine_strategy = COMBINE_UNION;
    properties_.tttr_data_valid = false;
}

void LayerNode::add_parent_node(LayerNode* parent_node) {
    if (parent_node && std::find(parent_nodes_.begin(), parent_nodes_.end(), parent_node) == parent_nodes_.end()) {
        parent_nodes_.push_back(parent_node);
        invalidate_tttr_data();
    }
}

void LayerNode::remove_parent_node(LayerNode* parent_node) {
    auto it = std::find(parent_nodes_.begin(), parent_nodes_.end(), parent_node);
    if (it != parent_nodes_.end()) {
        parent_nodes_.erase(it);
        invalidate_tttr_data();
    }
}

void LayerNode::execute_operation() {
    if (parent_nodes_.empty()) {
        // No parents - initialize with empty data
        tttr_indices_.reset();
        properties_.tttr_data_valid = true;
    } else if (parent_nodes_.size() == 1) {
        // Single parent - this is typically a split operation
        // For individual nodes, we don't split here but rather get data from parent
        if (properties_.operation == OPERATION_SPLIT || properties_.operation == OPERATION_PASSTHROUGH) {
            // Copy data from parent (actual splitting happens at layer level)
            const auto& parent_indices = parent_nodes_[0]->get_tttr_indices();
            if(!parent_indices.empty()){
                tttr_indices_ = std::make_unique<indices_set>(parent_indices);
            } else {
                tttr_indices_.reset();
            }
            properties_.tttr_data_valid = true;
        }
    } else {
        // Multiple parents - execute combine operation
        execute_combine(parent_nodes_);
    }
}

void LayerNode::set_split_strategy(uint32_t strategy, const std::map<std::string, double>& parameters) {
    properties_.split_strategy = strategy;
    operation_parameters_.insert(parameters.begin(), parameters.end());
    invalidate_tttr_data();
}

void LayerNode::set_custom_split_function(std::function<void(LayerNode*, const std::vector<LayerNode*>&)> split_func) {
    custom_split_func_ = split_func;
    properties_.split_strategy = SPLIT_CUSTOM;
    invalidate_tttr_data();
}

void LayerNode::execute_split(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes) {
    if (!parent_node || child_nodes.empty()) return;

    switch (properties_.split_strategy) {
        case SPLIT_BY_MARKERS:
            split_by_markers(parent_node, child_nodes);
            break;
        case SPLIT_BY_TIME:
            split_by_time(parent_node, child_nodes);
            break;
        case SPLIT_BY_COUNT:
            split_by_count(parent_node, child_nodes);
            break;
        case SPLIT_BY_POSITION:
            split_by_position(parent_node, child_nodes);
            break;
        case SPLIT_CUSTOM:
            if (custom_split_func_) {
                custom_split_func_(parent_node, child_nodes);
            }
            break;
    }
}

void LayerNode::set_combine_strategy(uint32_t strategy, const std::map<std::string, double>& parameters) {
    properties_.combine_strategy = strategy;
    operation_parameters_.insert(parameters.begin(), parameters.end());
    invalidate_tttr_data();
}

void LayerNode::set_custom_combine_function(std::function<void(const std::vector<LayerNode*>&, LayerNode*)> combine_func) {
    custom_combine_func_ = combine_func;
    properties_.combine_strategy = COMBINE_CUSTOM;
    invalidate_tttr_data();
}

void LayerNode::execute_combine(const std::vector<LayerNode*>& parent_nodes) {
    if (parent_nodes.empty()) {
        tttr_indices_.reset();
        properties_.tttr_data_valid = true;
        return;
    }

    switch (properties_.combine_strategy) {
        case COMBINE_UNION:
            combine_by_union(parent_nodes);
            break;
        case COMBINE_INTERSECTION:
            combine_by_intersection(parent_nodes);
            break;
        case COMBINE_WEIGHTED:
            combine_by_weighting(parent_nodes);
            break;
        case COMBINE_CUSTOM:
            if (custom_combine_func_) {
                custom_combine_func_(parent_nodes, this);
            }
            break;
    }
    properties_.tttr_data_valid = true;
}

const LayerNode::indices_set& LayerNode::get_tttr_indices() const {
    ensure_tttr_data_valid();
    static const indices_set empty_set;
    return tttr_indices_ ? *tttr_indices_ : empty_set;
}

void LayerNode::set_tttr_indices(const LayerNode::indices_set& indices) {
    if(!tttr_indices_){
        tttr_indices_ = std::make_unique<indices_set>();
    }
    *tttr_indices_ = indices;
    properties_.tttr_data_valid = true;
}

void LayerNode::add_tttr_indices(const LayerNode::indices_set& indices) {
    if(!tttr_indices_){
        tttr_indices_ = std::make_unique<indices_set>();
    }
    // Bulk insert is more efficient
    tttr_indices_->insert(indices.begin(), indices.end());
    properties_.tttr_data_valid = true;
}

void LayerNode::clear_tttr_indices() {
    tttr_indices_.reset();  // Deallocate memory
    properties_.tttr_data_valid = true;
}

void LayerNode::update_tttr_data() {
    execute_operation();
}

std::map<std::string, double> LayerNode::get_node_stats() const {
    std::map<std::string, double> stats;
    stats["tttr_count"] = static_cast<double>(size());
    stats["parent_count"] = static_cast<double>(parent_nodes_.size());
    stats["operation_type"] = static_cast<double>(properties_.operation);
    stats["split_strategy"] = static_cast<double>(properties_.split_strategy);
    stats["combine_strategy"] = static_cast<double>(properties_.combine_strategy);
    return stats;
}

// ===== Split Strategy Implementations =====

void LayerNode::split_by_markers(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes) {
    // Basic implementation - distribute indices evenly
    // In practice, this would use actual TTTR markers
    const auto& parent_indices = parent_node->get_tttr_indices();
    size_t indices_per_child = parent_indices.size() / child_nodes.size();
    size_t remainder = parent_indices.size() % child_nodes.size();
    
    auto it = parent_indices.begin();
    for (size_t i = 0; i < child_nodes.size(); ++i) {
        size_t count = indices_per_child + (i < remainder ? 1 : 0);
        LayerNode::indices_set child_indices;
        
        for (size_t j = 0; j < count && it != parent_indices.end(); ++j, ++it) {
            child_indices.insert(*it);
        }
        
        child_nodes[i]->set_tttr_indices(child_indices);
    }
}

void LayerNode::split_by_time(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes) {
    // Placeholder implementation - would use actual time information
    split_by_markers(parent_node, child_nodes);
}

void LayerNode::split_by_count(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes) {
    // Split by equal photon count
    split_by_markers(parent_node, child_nodes);
}

void LayerNode::split_by_position(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes) {
    // Placeholder implementation - would use spatial position information
    split_by_markers(parent_node, child_nodes);
}

// ===== Combine Strategy Implementations =====

void LayerNode::combine_by_union(const std::vector<LayerNode*>& parent_nodes) {
    tttr_indices_.reset();
    for (const auto* parent : parent_nodes) {
        if (parent) {
            const auto& parent_indices = parent->get_tttr_indices();
            if(!parent_indices.empty()){
                if(!tttr_indices_){
                    tttr_indices_ = std::make_unique<indices_set>();
                }
                tttr_indices_->insert(parent_indices.begin(), parent_indices.end());
            }
        }
    }
}

void LayerNode::combine_by_intersection(const std::vector<LayerNode*>& parent_nodes) {
    if (parent_nodes.empty()) {
        tttr_indices_.reset();
        return;
    }

    // Start with first parent's indices
    const auto& first_parent = parent_nodes[0]->get_tttr_indices();
    if(!first_parent.empty()){
        tttr_indices_ = std::make_unique<indices_set>(first_parent);
    } else {
        tttr_indices_.reset();
        return;
    }
    
    // Intersect with each subsequent parent
    for (size_t i = 1; i < parent_nodes.size(); ++i) {
        if (parent_nodes[i]) {
            const auto& parent_indices = parent_nodes[i]->get_tttr_indices();
            auto intersection = std::make_unique<indices_set>();
            
            // Manual intersection - only keep indices that exist in both sets
            for (const auto& idx : *tttr_indices_) {
                if (parent_indices.find(idx) != parent_indices.end()) {
                    intersection->insert(idx);
                }
            }
            
            tttr_indices_ = std::move(intersection);
        }
    }
}

void LayerNode::combine_by_weighting(const std::vector<LayerNode*>& parent_nodes) {
    // For now, just use union - weighting would require additional metadata
    combine_by_union(parent_nodes);
}

// ===== CLSMLayerAdapter Template Specializations =====

// CLSMPixel template specializations removed - CLSMPixel now directly inherits from LayerNode
// and handles synchronization internally through its overridden methods

// Template specializations removed to avoid access violations
// These will be implemented when CLSM classes are properly refactored
// to inherit from LayerNode with appropriate access patterns

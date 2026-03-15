#ifndef TTTRLIB_GRAPHNODE_H
#define TTTRLIB_GRAPHNODE_H

#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <climits>

/**
 * @brief Base class for graph node functionality in CLSM hierarchy.
 * 
 * This class provides parent-child relationship management for CLSM components
 * (CLSMImage, CLSMFrame, CLSMLine, CLSMPixel) forming a hierarchical graph structure.
 */
class GraphNode {

protected:
    /// Vector of child nodes (parent-child relationships managed through children only)
    std::vector<GraphNode*> _children;

public:
    
    /**
     * @brief Default constructor.
     */
    GraphNode() = default;
    
    /**
     * @brief Constructor with parent.
     * @param parent Pointer to parent node.
     */
    explicit GraphNode(GraphNode* parent) {
        if (parent != nullptr) {
            parent->add_child(this);
        }
    }
    
    /**
     * @brief Copy constructor.
     * @param other The GraphNode to copy from.
     */
    GraphNode(const GraphNode& other) {
        // Note: We don't copy parent/child relationships in copy constructor
        // These should be established explicitly when needed
    }
    
    /**
     * @brief Assignment operator.
     * @param other The GraphNode to assign from.
     * @return Reference to this GraphNode.
     */
    GraphNode& operator=(const GraphNode& other) {
        if (this != &other) {
            // Note: We don't copy parent/child relationships in assignment
            // These should be managed explicitly
        }
        return *this;
    }
    
    /**
     * @brief Virtual destructor.
     */
    virtual ~GraphNode() {
        // Clear children (no parent pointer to manage)
        _children.clear();
    }
    
    /**
     * @brief Find parent node by searching through potential parents.
     * @param potential_parents Vector of nodes to search for this node's parent.
     * @return Pointer to parent node, or nullptr if not found.
     * @note Without parent pointers, parent must be found by searching.
     */
    GraphNode* find_parent(const std::vector<GraphNode*>& potential_parents) const {
        for (GraphNode* candidate : potential_parents) {
            if (candidate && std::find(candidate->_children.begin(), candidate->_children.end(), this) != candidate->_children.end()) {
                return candidate;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get the children nodes.
     * @return Vector of pointers to child nodes.
     */
    const std::vector<GraphNode*>& get_children() const {
        return _children;
    }
    
    /**
     * @brief Get the number of children.
     * @return Number of child nodes.
     */
    size_t get_child_count() const {
        return _children.size();
    }
    
    /**
     * @brief Add a child node.
     * @param child Pointer to the child node to add.
     */
    void add_child(GraphNode* child) {
        if (child != nullptr) {
            // Check if child is already in the list
            auto it = std::find(_children.begin(), _children.end(), child);
            if (it == _children.end()) {
                _children.push_back(child);
                // No parent pointer to set - relationship managed through children only
            }
        }
    }
    
    /**
     * @brief Remove a child node.
     * @param child Pointer to the child node to remove.
     */
    void remove_child(GraphNode* child) {
        if (child != nullptr) {
            auto it = std::find(_children.begin(), _children.end(), child);
            if (it != _children.end()) {
                _children.erase(it);
                // No parent pointer to clear - relationship managed through children only
            }
        }
    }
    
    /**
     * @brief Check if this node is a root node (has no parent).
     * @param potential_parents Vector of nodes to search for this node's parent.
     * @return True if this is a root node, false otherwise.
     * @note Without parent pointers, must search to determine if root.
     */
    bool is_root(const std::vector<GraphNode*>& potential_parents) const {
        return find_parent(potential_parents) == nullptr;
    }
    
    /**
     * @brief Check if this node is a leaf node (has no children).
     * @return True if this is a leaf node, false otherwise.
     */
    bool is_leaf() const {
        return _children.empty();
    }
    
    /**
     * @brief Get the depth of this node in the graph (distance from root).
     * @param root_node Pointer to the root node to calculate depth from.
     * @return Depth level (0 for root, 1 for children of root, etc.).
     * @note Without parent pointers, depth must be calculated from root.
     */
    size_t get_depth(GraphNode* root_node) const {
        if (this == root_node) return 0;
        return calculate_depth_from_root(root_node, 0);
    }
    
    /**
     * @brief Find the root node by traversing up from a known hierarchy.
     * @param hierarchy_nodes Vector of all nodes in the hierarchy to search.
     * @return Pointer to the root node.
     * @note Without parent pointers, root must be found by searching hierarchy.
     */
    GraphNode* find_root(const std::vector<GraphNode*>& hierarchy_nodes) {
        for (GraphNode* candidate : hierarchy_nodes) {
            if (candidate && candidate->is_root(hierarchy_nodes)) {
                return candidate;
            }
        }
        return nullptr; // No root found
    }
    
    /**
     * @brief Get all descendant nodes (children, grandchildren, etc.).
     * @return Vector of pointers to all descendant nodes.
     */
    std::vector<GraphNode*> get_descendants() {
        std::vector<GraphNode*> descendants;
        collect_descendants(descendants);
        return descendants;
    }
    
    /**
     * @brief Get all ancestor nodes (parent, grandparent, etc.).
     * @param hierarchy_nodes Vector of all nodes in the hierarchy to search.
     * @return Vector of pointers to all ancestor nodes (from immediate parent to root).
     * @note Without parent pointers, ancestors must be found by searching hierarchy.
     */
    std::vector<GraphNode*> get_ancestors(const std::vector<GraphNode*>& hierarchy_nodes) {
        std::vector<GraphNode*> ancestors;
        GraphNode* current_parent = find_parent(hierarchy_nodes);
        while (current_parent != nullptr) {
            ancestors.push_back(current_parent);
            current_parent = current_parent->find_parent(hierarchy_nodes);
        }
        return ancestors;
    }

protected:
    /**
     * @brief Synchronize GraphNode children with a container of objects.
     * 
     * This template method synchronizes the GraphNode children vector with
     * any container (like std::vector) of objects that inherit from GraphNode.
     * Uses lazy evaluation to avoid excessive sync operations and prevent memory bloat.
     * 
     * @tparam Container The container type (e.g., std::vector<CLSMPixel>)
     * @param container The container whose elements should become children
     * @param force_sync Force synchronization even if not needed
     */
    template<typename Container>
    void sync_children_with_container(Container& container, bool force_sync = false) {
        // Lazy sync - only sync if container size changed or forced
        if (!force_sync && _children.size() == container.size()) {
            return; // Skip sync if sizes match to prevent memory bloat
        }
        
        // Clear existing children (no parent pointers to manage)
        _children.clear();
        
        // Add container elements as children with optimized memory management
        _children.reserve(container.size());
        for(auto& element : container) {
            _children.push_back(&element);
            // No parent pointer to set - relationship managed through children only
        }
    }

private:
    
    /**
     * @brief Recursively collect all descendant nodes.
     * @param descendants Vector to store descendant nodes.
     */
    void collect_descendants(std::vector<GraphNode*>& descendants) {
        for (GraphNode* child : _children) {
            descendants.push_back(child);
            child->collect_descendants(descendants);
        }
    }
    
    /**
     * @brief Helper method to calculate depth from root node recursively.
     * @param root_node Root node to calculate depth from.
     * @param current_depth Current depth in recursion.
     * @return Depth of this node from root.
     */
    size_t calculate_depth_from_root(GraphNode* root_node, size_t current_depth) const {
        if (this == root_node) return current_depth;
        
        for (GraphNode* child : root_node->_children) {
            if (child) {
                size_t child_depth = calculate_depth_from_root(child, current_depth + 1);
                if (child_depth != SIZE_MAX) return child_depth; // Found
            }
        }
        return SIZE_MAX; // Not found in this subtree
    }
};

#endif //TTTRLIB_GRAPHNODE_H

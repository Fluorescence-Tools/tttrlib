#ifndef TTTRLIB_LAYERNODE_H
#define TTTRLIB_LAYERNODE_H

#include "GraphNode.h"
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <functional>
#include <set>

/*!
 * \brief Base class for nodes in a layer-based architecture with split/combine operations.
 * 
 * LayerNode provides a flexible framework where each node can have:
 * - Single parent → Split operation (e.g., CLSMImage splits to Lines)
 * - Multiple parents → Combine operation (e.g., multiple Pixels combine for binning)
 * 
 * This maintains backward compatibility with existing CLSM hierarchy while enabling
 * flexible transformations and compositions.
 */
class LayerNode : public virtual GraphNode {

public:
    /*!
     * \brief Compact bitfield structure combining all LayerNode properties.
     * 
     * This replaces separate enum variables with a single 32-bit bitfield,
     * saving significant memory (12+ bytes per node reduced to 4 bytes).
     */
    struct NodeProperties {
        union {
            struct {
                // Node operation (3 bits - supports up to 8 operations)
                unsigned int operation : 3;
                
                // Split strategy (3 bits - supports up to 8 strategies)  
                unsigned int split_strategy : 3;
                
                // Combine strategy (3 bits - supports up to 8 strategies)
                unsigned int combine_strategy : 3;
                
                // Status flags (4 bits)
                unsigned int tttr_data_valid : 1;
                unsigned int has_custom_split : 1;
                unsigned int has_custom_combine : 1;
                unsigned int reserved_flag : 1;
                
                // Reserved for future use (19 bits)
                unsigned int reserved : 19;
            };
            uint32_t raw;  // Raw access to all bits
        };
        
        NodeProperties() : raw(0) {}  // Initialize all bits to 0
    };

    // Operation constants (replacing enum class NodeOperation)
    static constexpr unsigned int OP_SPLIT = 0;
    static constexpr unsigned int OP_COMBINE = 1;
    static constexpr unsigned int OP_PASSTHROUGH = 2;
    static constexpr unsigned int OPERATION_SPLIT = 0;      // Alias for backward compatibility
    static constexpr unsigned int OPERATION_PASSTHROUGH = 2; // Alias for backward compatibility
    
    // Split strategy constants (replacing enum class SplitStrategy)
    static constexpr unsigned int SPLIT_BY_MARKERS = 0;
    static constexpr unsigned int SPLIT_BY_TIME = 1;
    static constexpr unsigned int SPLIT_BY_COUNT = 2;
    static constexpr unsigned int SPLIT_BY_POSITION = 3;
    static constexpr unsigned int SPLIT_CUSTOM = 4;
    
    // Combine strategy constants (replacing enum class CombineStrategy)
    static constexpr unsigned int COMBINE_UNION = 0;
    static constexpr unsigned int COMBINE_INTERSECTION = 1;
    static constexpr unsigned int COMBINE_WEIGHTED = 2;
    static constexpr unsigned int COMBINE_CUSTOM = 3;

protected:
    // Compact bitfield combining all properties (saves 12+ bytes per node)
    NodeProperties properties_;
    
    // TTTR data - the core of each node with lazy allocation
    /// Using unique_ptr to avoid overhead for empty containers
    using indices_set = std::set<int>;
    std::unique_ptr<indices_set> tttr_indices_;
    
    // Multiple parent support
    std::vector<LayerNode*> parent_nodes_;
    
    // Parameters for operations
    std::map<std::string, double> operation_parameters_;
    
    // Custom operation functions (only allocated when needed)
    std::function<void(LayerNode*, const std::vector<LayerNode*>&)> custom_split_func_;
    std::function<void(const std::vector<LayerNode*>&, LayerNode*)> custom_combine_func_;

public:

    /*!
     * \brief Constructor for LayerNode.
     * 
     * \param operation     Node operation type (using bitfield constants)
     * \param parent        Parent GraphNode (for GraphNode hierarchy)
     */
    LayerNode(
        unsigned int operation = OP_PASSTHROUGH,
        GraphNode* parent = nullptr
    );

    /*!
     * \brief Virtual destructor.
     */
    virtual ~LayerNode() = default;

    // === Core Node Operations ===

    /*!
     * \brief Add a parent node for this node.
     * 
     * \param parent_node Parent to add
     */
    void add_parent_node(LayerNode* parent_node);

    /*!
     * \brief Remove a parent node.
     * 
     * \param parent_node Parent to remove
     */
    void remove_parent_node(LayerNode* parent_node);

    /*!
     * \brief Get all parent nodes.
     */
    const std::vector<LayerNode*>& get_parent_nodes() const { return parent_nodes_; }

    /*!
     * \brief Execute the node operation (split or combine).
     * 
     * This method performs the appropriate operation based on the number of parents:
     * - 0 parents: Initialize with empty TTTR data
     * - 1 parent: Execute split operation
     * - Multiple parents: Execute combine operation
     */
    virtual void execute_operation();

    // === Split Operations ===

    /*!
     * \brief Set split strategy and parameters.
     * 
     * \param strategy Split strategy to use (using bitfield constants)
     * \param parameters Strategy-specific parameters
     */
    void set_split_strategy(unsigned int strategy, const std::map<std::string, double>& parameters = {});

    /*!
     * \brief Set custom split function.
     * 
     * \param split_func Custom function for splitting TTTR data
     */
    void set_custom_split_function(std::function<void(LayerNode*, const std::vector<LayerNode*>&)> split_func);

    /*!
     * \brief Execute split operation from single parent to multiple children.
     * 
     * \param parent_node Single parent node
     * \param child_nodes Vector of child nodes to populate
     */
    virtual void execute_split(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes);

    // === Combine Operations ===

    /*!
     * \brief Set combine strategy and parameters.
     * 
     * \param strategy Combine strategy to use (using bitfield constants)
     * \param parameters Strategy-specific parameters
     */
    void set_combine_strategy(unsigned int strategy, const std::map<std::string, double>& parameters = {});

    /*!
     * \brief Set custom combine function.
     * 
     * \param combine_func Custom function for combining TTTR data
     */
    void set_custom_combine_function(const std::function<void(const std::vector<LayerNode*>&, LayerNode*)>& combine_func);

    /*!
     * \brief Execute combine operation from multiple parents to single child.
     * 
     * \param parent_nodes Vector of parent nodes
     */
    virtual void execute_combine(const std::vector<LayerNode*>& parent_nodes);

    // === TTTR Data Access ===

    /*!
     * \brief Get TTTR indices for this node.
     */
    virtual const indices_set& get_tttr_indices() const;

    /*!
     * \brief Set TTTR indices directly.
     * 
     * \param indices TTTR indices to set
     */
    virtual void set_tttr_indices(const indices_set& indices);

    /*!
     * \brief Add TTTR indices to this node.
     * 
     * \param indices TTTR indices to add
     */
    void add_tttr_indices(const indices_set& indices);

    /*!
     * \brief Clear TTTR indices.
     */
    void clear_tttr_indices();

    /*!
     * \brief Get number of TTTR events in this node.
     */
    size_t size() const { return tttr_indices_ ? tttr_indices_->size() : 0; }

    // === Accessors ===

    unsigned int get_operation() const { return properties_.operation; }
    void set_operation(unsigned int operation) { properties_.operation = operation; invalidate_tttr_data(); }

    unsigned int get_split_strategy() const { return properties_.split_strategy; }
    unsigned int get_combine_strategy() const { return properties_.combine_strategy; }

    const std::map<std::string, double>& get_operation_parameters() const { return operation_parameters_; }
    void set_operation_parameter(const std::string& name, double value) { operation_parameters_[name] = value; }

    // === Utility Methods ===

    /*!
     * \brief Check if TTTR data is valid/up-to-date.
     */
    bool is_tttr_data_valid() const { return properties_.tttr_data_valid; }

    /*!
     * \brief Invalidate TTTR data, forcing recalculation on next access.
     */
    void invalidate_tttr_data() { properties_.tttr_data_valid = 0; }

    /*!
     * \brief Update TTTR data by executing the appropriate operation.
     */
    void update_tttr_data();

    /*!
     * \brief Get statistics about this node.
     */
    virtual std::map<std::string, double> get_node_stats() const;

protected:
    /*!
     * \brief Ensure TTTR data is up to date.
     */
    void ensure_tttr_data_valid() const {
        if (!properties_.tttr_data_valid) {
            const_cast<LayerNode*>(this)->update_tttr_data();
        }
    }

    // === Built-in Split Strategies ===

    /*!
     * \brief Split by TTTR markers (lines, frames, etc.).
     */
    virtual void split_by_markers(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes);

    /*!
     * \brief Split by time intervals.
     */
    virtual void split_by_time(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes);

    /*!
     * \brief Split by photon count.
     */
    virtual void split_by_count(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes);

    /*!
     * \brief Split by spatial position.
     */
    virtual void split_by_position(LayerNode* parent_node, const std::vector<LayerNode*>& child_nodes);

    // === Built-in Combine Strategies ===

    /*!
     * \brief Combine by union (default for binning).
     */
    virtual void combine_by_union(const std::vector<LayerNode*>& parent_nodes);

    /*!
     * \brief Combine by intersection.
     */
    virtual void combine_by_intersection(const std::vector<LayerNode*>& parent_nodes);

    /*!
     * \brief Combine with weighting.
     */
    virtual void combine_by_weighting(const std::vector<LayerNode*>& parent_nodes);
};

/*!
 * \brief Adapter class to integrate LayerNode with existing CLSM classes.
 * 
 * This provides backward compatibility by allowing existing CLSMPixel, CLSMLine,
 * CLSMFrame, and CLSMImage classes to optionally use the new LayerNode system
 * while maintaining their existing APIs.
 */
template<typename CLSMType>
class CLSMLayerAdapter {
private:
    CLSMType* clsm_object_;                    ///< Existing CLSM object
    std::unique_ptr<LayerNode> layer_node_;   ///< Optional LayerNode for new functionality

public:
    /*!
     * \brief Constructor with existing CLSM object.
     * 
     * \param clsm_object Existing CLSM object to adapt
     * \param enable_layer_node Whether to enable LayerNode functionality
     */
    CLSMLayerAdapter(CLSMType* clsm_object, bool enable_layer_node = false)
        : clsm_object_(clsm_object) {
        if (enable_layer_node) {
            layer_node_ = std::make_unique<LayerNode>();
            // Sync initial TTTR data from CLSM object to LayerNode
            sync_to_layer_node();
        }
    }

    /*!
     * \brief Get the underlying CLSM object.
     */
    CLSMType* get_clsm_object() { return clsm_object_; }
    const CLSMType* get_clsm_object() const { return clsm_object_; }

    /*!
     * \brief Get the LayerNode (if enabled).
     */
    LayerNode* get_layer_node() { return layer_node_.get(); }
    const LayerNode* get_layer_node() const { return layer_node_.get(); }

    /*!
     * \brief Enable LayerNode functionality.
     */
    void enable_layer_node() {
        if (!layer_node_) {
            layer_node_ = std::make_unique<LayerNode>();
            sync_to_layer_node();
        }
    }

    /*!
     * \brief Disable LayerNode functionality.
     */
    void disable_layer_node() {
        if (layer_node_) {
            sync_from_layer_node();
            layer_node_.reset();
        }
    }

    /*!
     * \brief Sync TTTR data from CLSM object to LayerNode.
     */
    void sync_to_layer_node();

    /*!
     * \brief Sync TTTR data from LayerNode back to CLSM object.
     */
    void sync_from_layer_node();

    /*!
     * \brief Check if LayerNode functionality is enabled.
     */
    bool is_layer_node_enabled() const { return layer_node_ != nullptr; }
};

// Type aliases for common adapters
// Commented out to avoid circular dependencies - will be defined in separate header
// using CLSMPixelAdapter = CLSMLayerAdapter<CLSMPixel>;
// using CLSMLineAdapter = CLSMLayerAdapter<CLSMLine>;
// using CLSMFrameAdapter = CLSMLayerAdapter<CLSMFrame>;
// using CLSMImageAdapter = CLSMLayerAdapter<CLSMImage>;

#endif // TTTRLIB_LAYERNODE_H

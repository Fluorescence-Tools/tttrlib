#ifndef TTTRLIB_BIMAP_H
#define TTTRLIB_BIMAP_H

#include <map>

namespace tttrlib {

/**
 * @brief Bidirectional map container
 * 
 * Provides a lightweight bidirectional mapping between two types.
 * Supports insert and lookup in both directions.
 * Compatible with boost::bimap interface (.left, .right accessors).
 * 
 * @tparam Left Type for left-side keys
 * @tparam Right Type for right-side keys
 */
template<typename Left, typename Right>
class bimap {
private:
    std::map<Left, Right> left_to_right_map;
    std::map<Right, Left> right_to_left_map;

public:
    /**
     * @brief Left view accessor proxy
     */
    class LeftViewProxy {
        const bimap& parent;
    public:
        explicit LeftViewProxy(const bimap& p) : parent(p) {}
        auto begin() const { return parent.left_to_right_map.begin(); }
        auto end() const { return parent.left_to_right_map.end(); }
        auto cbegin() const { return parent.left_to_right_map.cbegin(); }
        auto cend() const { return parent.left_to_right_map.cend(); }
        size_t size() const { return parent.left_to_right_map.size(); }
        bool empty() const { return parent.left_to_right_map.empty(); }
        Right at(const Left& key) const { return parent.left_to_right_map.at(key); }
    };

    /**
     * @brief Right view accessor proxy
     */
    class RightViewProxy {
        const bimap& parent;
    public:
        explicit RightViewProxy(const bimap& p) : parent(p) {}
        auto begin() const { return parent.right_to_left_map.begin(); }
        auto end() const { return parent.right_to_left_map.end(); }
        auto cbegin() const { return parent.right_to_left_map.cbegin(); }
        auto cend() const { return parent.right_to_left_map.cend(); }
        size_t size() const { return parent.right_to_left_map.size(); }
        bool empty() const { return parent.right_to_left_map.empty(); }
        Left at(const Right& key) const { return parent.right_to_left_map.at(key); }
    };

    /**
     * @brief Access left-to-right view (compatible with boost::bimap.left)
     * Returns a temporary proxy that can be used for iteration and lookup
     */
    LeftViewProxy left{*this};

    /**
     * @brief Access right-to-left view (compatible with boost::bimap.right)
     * Returns a temporary proxy that can be used for iteration and lookup
     */
    RightViewProxy right{*this};

    /**
     * @brief Insert a pair into the bimap
     * @param p Pair of (left, right) values
     */
    void insert(const std::pair<Left, Right>& p) {
        left_to_right_map[p.first] = p.second;
        right_to_left_map[p.second] = p.first;
    }

    /**
     * @brief Insert a pair into the bimap (convenience overload)
     */
    void insert(const Left& l, const Right& r) {
        left_to_right_map[l] = r;
        right_to_left_map[r] = l;
    }

    /**
     * @brief Get right value from left key
     */
    Right at_left(const Left& key) const {
        return left_to_right_map.at(key);
    }

    /**
     * @brief Get left value from right key
     */
    Left at_right(const Right& key) const {
        return right_to_left_map.at(key);
    }

    /**
     * @brief Check if left key exists
     */
    bool count_left(const Left& key) const {
        return left_to_right_map.count(key) > 0;
    }

    /**
     * @brief Check if right key exists
     */
    bool count_right(const Right& key) const {
        return right_to_left_map.count(key) > 0;
    }

    /**
     * @brief Get size
     */
    size_t size() const {
        return left_to_right_map.size();
    }

    /**
     * @brief Clear the bimap
     */
    void clear() {
        left_to_right_map.clear();
        right_to_left_map.clear();
    }
};

} // namespace tttrlib

#endif // TTTRLIB_BIMAP_H

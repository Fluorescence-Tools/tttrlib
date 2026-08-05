// SPDX-License-Identifier: BSD-3-Clause
#include "DataStore.h"

namespace tttrlib {
namespace data {

/*!
 * The single registry instance.
 *
 * Out of line on purpose: as an inline function-local static it was duplicated
 * between the core library and the Python extension, because the extension is
 * built with hidden visibility and the symbol could not be merged. Stores
 * created on one side were then invisible to a listing taken from the other.
 */
DataStoreRegistry& DataStoreRegistry::instance() {
    static DataStoreRegistry r;
    return r;
}

}  // namespace data
}  // namespace tttrlib

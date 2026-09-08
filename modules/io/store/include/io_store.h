// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_STORE_H
#define TTTRLIB_IO_STORE_H

/*!
 * \file io_store.h
 * \brief The native store file (.dstore): a DataStore, saved and reloaded.
 *
 * The format and its reader and writer live in **ptolib**
 * (`thirdparty/ptolib/ptolib.h`, see `docs/dstore.md` there for the layout and
 * the five choices behind it). This header re-exports them under `tttrlib::io`,
 * where every caller and binding has always found them. Nothing here is
 * tttrlib-specific; a `.dstore` written by IMP.bff through the same header is
 * the file this reads.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef SWIG
#include <cstdio>
#endif

#include "ptolib/ptolib.h"
#include "DataStore.h"

namespace tttrlib {
namespace io {

using pto::kStoreMagic;
using pto::kStoreExtension;
using pto::write_store;
using pto::read_store_into;
using pto::read_store;
using pto::is_store_file;
using pto::write_store_at;
using pto::store_has;
using pto::store_columns;
using pto::store_groups;
using pto::store_bytes_read;

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_STORE_H

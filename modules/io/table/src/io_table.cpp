// SPDX-License-Identifier: BSD-3-Clause
#include "io_table.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "TTTRFormat.h"
#include "io_csv.h"
#include "io_csv_writer.h"
#include "io_hdf5_table.h"
#include "io_pto.h"
#include "io_store.h"

namespace tttrlib {
namespace io {

namespace {

/// The extension, lower-cased, without the dot. Empty when there is none.
std::string extension_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    if (slash != std::string::npos && dot < slash) return std::string();
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return ext;
}

/// Whether the file starts with these bytes. The cheapest question there is,
/// and the one that makes content beat the extension.
bool starts_with(const std::string& path, const unsigned char* magic,
                 std::size_t n) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    unsigned char head[16];
    const std::size_t got = std::fread(head, 1, n > 16 ? 16 : n, f);
    std::fclose(f);
    return got == n && std::memcmp(head, magic, n) == 0;
}

bool looks_like_hdf5(const std::string& path) {
    static const unsigned char sig[8] =
        {0x89, 'H', 'D', 'F', 0x0D, 0x0A, 0x1A, 0x0A};
    return starts_with(path, sig, 8);
}

/// EBML, which is what a PTO is framed in. The DocType check is the container's
/// own job -- this only has to be sure enough to route.
bool looks_like_ebml(const std::string& path) {
    static const unsigned char sig[4] = {0x1A, 0x45, 0xDF, 0xA3};
    return starts_with(path, sig, 4);
}

/// A leading and a trailing separator are optional everywhere a group path is
/// taken, and HDF5 lists `/results` where the native format lists `results`.
/// Normalising here is what lets a path from one listing be handed to the other.
std::string bare_group(const std::string& group) {
    std::size_t b = 0, e = group.size();
    while (b < e && group[b] == '/') b++;
    while (e > b && group[e - 1] == '/') e--;
    return group.substr(b, e - b);
}

/// The one place a knob a format does not have is refused. Ignoring it would
/// give a caller a whole-file read where they asked for part of one, which is
/// the failure this whole surface exists to remove.
void refuse(const char* verb, const char* what, TableFormat f,
            const std::string& spec) {
    throw std::runtime_error(std::string(verb) + ": " + what +
                             " is not something a " + table_format_name(f) +
                             " file has (" + spec + ")");
}

/// The object a `path|name` spec names, by name or by decimal uid.
PtoObject pto_object(const PtoFile& file, const std::string& selector,
                     const std::string& spec) {
    const std::vector<PtoObject> all = file.objects();
    for (const PtoObject& o : all)
        if (o.name == selector) return o;
    // A uid is what the C++ API takes, so a caller holding one should not have
    // to look up a name to use this surface.
    if (!selector.empty() &&
        selector.find_first_not_of("0123456789") == std::string::npos) {
        const std::uint64_t uid = std::strtoull(selector.c_str(), nullptr, 10);
        for (const PtoObject& o : all)
            if (o.uid == uid) return o;
    }
    std::string names;
    for (const PtoObject& o : all) {
        if (!names.empty()) names += ", ";
        names += o.name;
    }
    throw std::runtime_error("read_table: no object '" + selector + "' in " +
                             spec + " (it holds: " + names + ")");
}

}  // namespace

const char* table_format_name(TableFormat f) {
    switch (f) {
        case TableFormat::Store: return "dstore";
        case TableFormat::Hdf5:  return "HDF5";
        case TableFormat::Csv:   return "CSV";
        case TableFormat::Pto:   return "PTO";
        case TableFormat::Unknown: break;
    }
    return "unknown";
}

TableFormat table_format_of(const std::string& spec) {
    const std::string path = subfile_path(spec);
    const std::string ext = extension_of(path);

    // Content first. An extension is a claim and the bytes are the fact, and
    // two of the three formats a table can be in are also formats something
    // else can be in -- `.h5` is a Photon-HDF5 as often as a table.
    if (is_store_file(path)) return TableFormat::Store;
    if (looks_like_ebml(path)) return TableFormat::Pto;
    if (looks_like_hdf5(path)) return TableFormat::Hdf5;

    // CSV has no magic bytes and never will, so it is the one decided by name.
    // Worth knowing before relying on it: a `.csv` that is not one fails at the
    // reader rather than here.
    if (ext == "csv" || ext == "tsv" || ext == "txt") return TableFormat::Csv;

    // The file may not exist yet, which is the write path. Fall back to what
    // the name claims rather than refusing to route it.
    if (ext == "dstore") return TableFormat::Store;
    if (ext == "pto") return TableFormat::Pto;
    if (ext == "h5" || ext == "hdf5" || ext == "he5") return TableFormat::Hdf5;
    return TableFormat::Unknown;
}

void read_table_into(data::DataStore& out, const std::string& spec,
                     const std::string& group,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows) {
    const TableFormat f = table_format_of(spec);
    const std::string path = subfile_path(spec);
    const std::string want = bare_group(group);

    switch (f) {
        case TableFormat::Store:
            read_store_into(out, path, 0, 0, columns, first_row, n_rows, want);
            return;

        case TableFormat::Hdf5:
            // HDF5 names its root "/" where the native format names it "".
            read_hdf5_table_into(out, path, want.empty() ? "/" : want, true,
                                 columns, first_row, n_rows);
            return;

        case TableFormat::Csv: {
            if (!want.empty()) refuse("read_table", "a group", f, spec);
            if (first_row != 0 || n_rows != 0)
                refuse("read_table", "a row range", f, spec);
            CsvOptions options;
            read_csv_into(out, path, options);
            if (!columns.empty()) {
                // The reader has no column filter, so this is a projection
                // after the fact -- honest about the cost rather than pretending
                // a text format can seek to a column.
                for (int i = out.n_columns() - 1; i >= 0; i--) {
                    const std::string& name = out.column(i).name();
                    if (std::find(columns.begin(), columns.end(), name) ==
                        columns.end())
                        out.remove_column(i);
                }
            }
            return;
        }

        case TableFormat::Pto: {
            PtoFile file;
            if (!file.open(path, false))
                throw std::runtime_error("read_table: cannot open " + path);
            const std::string selector = subfile_selector(spec);
            if (selector.empty())
                throw std::runtime_error(
                        "read_table: a PTO holds many objects, so name one: "
                        "\"" + path + "|<object>\"");
            const PtoObject o = pto_object(file, selector, spec);
            // An embedded store is a region of a bigger file, and the native
            // reader already takes every knob for a region -- so the group knob
            // needs nothing of PTO's own.
            read_store_into(out, path, o.offset, o.size, columns, first_row,
                            n_rows, want);
            return;
        }

        case TableFormat::Unknown:
            break;
    }
    throw std::runtime_error(
            "read_table: " + spec + " is not a table this library can read "
            "(looked for a .dstore directory, an EBML container, the HDF5 "
            "signature, and a .csv extension)");
}

data::DataStore read_table(const std::string& spec, const std::string& group,
                           const std::vector<std::string>& columns,
                           std::uint64_t first_row, std::uint64_t n_rows) {
    data::DataStore out;
    read_table_into(out, spec, group, columns, first_row, n_rows);
    return out;
}

bool write_table(const std::string& spec, const data::DataStore& store,
                 const std::string& group, bool replace_file) {
    const TableFormat f = table_format_of(spec);
    const std::string path = subfile_path(spec);
    const std::string want = bare_group(group);

    switch (f) {
        case TableFormat::Store: {
            if (want.empty()) return write_store(path, store);
            // A group write is a read, a replace and a write back. The format
            // holds one tree and cannot patch part of it in place, so this
            // rewrites -- which is the same rule the caller is already under
            // (a change happens in memory; a write puts it in a file) and is
            // why `rewrites_on_partial_write` is published in the registry.
            // The caller is not told which formats patch and which rewrite,
            // because the resulting file is the same either way.
            data::DataStore whole;
            try {
                read_store_into(whole, path, 0, 0, std::vector<std::string>(), 0, 0, "");
            } catch (const std::exception&) {
                // Nothing there yet: the group write creates the file.
            }
            whole.ensure_group(want) = store;
            return write_store(path, whole);
        }

        case TableFormat::Hdf5:
            return write_hdf5_table(path, store, want.empty() ? "/" : want, 0,
                                    replace_file ? Hdf5WriteMode::Truncate
                                                 : Hdf5WriteMode::Update);

        case TableFormat::Csv:
            if (!want.empty())
                refuse("write_table", "a group to write into", f, spec);
            return write_csv(path, store);

        case TableFormat::Pto: {
            const std::string selector = subfile_selector(spec);
            if (selector.empty())
                throw std::runtime_error(
                        "write_table: name the object to write: "
                        "\"" + path + "|<object>\"");
            PtoFile file;
            if (!file.open(path, true) && !file.create(path))
                throw std::runtime_error("write_table: cannot open " + path);
            // "table" is the object's kind; the encoding is `dstore` and the
            // writer picks it. An object of the same name already there is
            // updated rather than duplicated, matching what writing a group of
            // an HDF5 file does.
            for (const PtoObject& o : file.objects()) {
                if (o.name != selector || o.encoding != "dstore") continue;
                if (want.empty()) return pto_update_store(file, o.uid, store);
                // Same read-modify-write as the native format, for the same
                // reason: the object IS a store, so replacing one group of it
                // is replacing the object.
                data::DataStore whole;
                read_store_into(whole, path, o.offset, o.size,
                                std::vector<std::string>(), 0, 0, "");
                whole.ensure_group(want) = store;
                return pto_update_store(file, o.uid, whole);
            }
            if (want.empty())
                return pto_add_store(file, "table", selector, store) != 0;
            data::DataStore fresh;
            fresh.ensure_group(want) = store;
            return pto_add_store(file, "table", selector, fresh) != 0;
        }

        case TableFormat::Unknown:
            break;
    }
    throw std::runtime_error("write_table: " + spec +
                             " names no format this library writes a table to "
                             "(.dstore, .h5/.hdf5, .csv, .pto)");
}

std::vector<std::string> table_groups(const std::string& spec) {
    const std::string path = subfile_path(spec);
    try {
        switch (table_format_of(spec)) {
            case TableFormat::Store: return store_groups(path);
            case TableFormat::Hdf5: {
                // HDF5's own listing gives `/results` and includes the root as
                // `/`; the native format's gives `results` and does not. One of
                // them has to win here or a path taken from one listing cannot
                // be handed to the other, which is the whole point. The bare
                // form wins because it is the one a caller writes.
                std::vector<std::string> out;
                for (const std::string& g : hdf5_table_groups(path)) {
                    const std::string bare = bare_group(g);
                    if (!bare.empty()) out.push_back(bare);
                }
                return out;
            }
            case TableFormat::Csv:   return std::vector<std::string>();
            case TableFormat::Pto: {
                PtoFile file;
                if (!file.open(path, false)) return {};
                const std::string selector = subfile_selector(spec);
                if (selector.empty()) return {};
                return pto_store_groups(file, pto_object(file, selector, spec).uid);
            }
            case TableFormat::Unknown: break;
        }
    } catch (const std::exception&) {
        // A question, not a read: silent on any input, like the per-format
        // listings it delegates to.
    }
    return std::vector<std::string>();
}

std::vector<std::string> table_columns(const std::string& spec,
                                       const std::string& group) {
    const std::string path = subfile_path(spec);
    const std::string want = bare_group(group);
    try {
        switch (table_format_of(spec)) {
            case TableFormat::Store: return store_columns(path, want);
            case TableFormat::Hdf5:
                return read_hdf5_table_columns(path, want.empty() ? "/" : want);
            case TableFormat::Csv: {
                CsvOptions options;
                return read_csv_column_names(path, options);
            }
            case TableFormat::Pto: {
                PtoFile file;
                if (!file.open(path, false)) return {};
                const std::string selector = subfile_selector(spec);
                if (selector.empty()) return {};
                return pto_store_columns(
                        file, pto_object(file, selector, spec).uid, want);
            }
            case TableFormat::Unknown: break;
        }
    } catch (const std::exception&) {
    }
    return std::vector<std::string>();
}

bool table_has(const std::string& spec, const std::string& group) {
    const std::string path = subfile_path(spec);
    const std::string want = bare_group(group);
    try {
        switch (table_format_of(spec)) {
            case TableFormat::Store: return store_has(path, want);
            case TableFormat::Hdf5:
                return hdf5_table_has(path, want.empty() ? "/" : want);
            case TableFormat::Csv:
                // One flat table: it is there, and it has no groups.
                return want.empty() && !read_csv_column_names(
                        path, CsvOptions()).empty();
            case TableFormat::Pto: {
                PtoFile file;
                if (!file.open(path, false)) return false;
                const std::string selector = subfile_selector(spec);
                if (selector.empty()) return false;
                const PtoObject o = pto_object(file, selector, spec);
                if (o.encoding != "dstore") return false;
                if (want.empty()) return true;
                const std::vector<std::string> groups =
                        pto_store_groups(file, o.uid);
                return std::find(groups.begin(), groups.end(), want) !=
                       groups.end();
            }
            case TableFormat::Unknown: break;
        }
    } catch (const std::exception&) {
    }
    return false;
}

}  // namespace io
}  // namespace tttrlib

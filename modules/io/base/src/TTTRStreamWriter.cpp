// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRStreamWriter.h"

#include "TTTRFormat.h"

#include <string>

namespace tttrlib {
namespace io {

TTTRStreamWriter::TTTRStreamWriter() = default;
TTTRStreamWriter::~TTTRStreamWriter() = default;

bool TTTRStreamWriter::lengths_agree(std::size_t n_macro, std::size_t n_micro,
                                     std::size_t n_routing, std::size_t n_event) {
    if (n_macro == n_micro && n_macro == n_routing && n_macro == n_event) return true;
    // Named in full rather than "lengths differ": the caller is assembling
    // four arrays from an instrument and needs to know which one is short.
    return fail("the four event arrays have different lengths (macro " +
                std::to_string(n_macro) + ", micro " + std::to_string(n_micro) +
                ", routing " + std::to_string(n_routing) + ", event type " +
                std::to_string(n_event) + "); one photon per row in each");
}

namespace {

/// The factory a format registered, or null. \see FileFormat::make_stream_writer
std::unique_ptr<TTTRStreamWriter> from_format(const FileFormat* f) {
    if (f == nullptr || f->make_stream_writer == nullptr) return nullptr;
    void* raw = f->make_stream_writer(f->stream_context);
    return std::unique_ptr<TTTRStreamWriter>(static_cast<TTTRStreamWriter*>(raw));
}

}  // namespace

std::unique_ptr<TTTRStreamWriter> make_stream_writer(const std::string& filename) {
    // By extension, not by content: the file does not exist yet. A stream
    // writer is asked for before there is anything to sniff.
    const int container = IORegistry::container_type_from_extension(filename);
    if (container < 0) return nullptr;
    return make_stream_writer_for(container);
}

std::unique_ptr<TTTRStreamWriter> make_stream_writer_for(int container_type) {
    return from_format(IORegistry::by_container_type(container_type));
}

bool can_stream(int container_type) {
    const FileFormat* f = IORegistry::by_container_type(container_type);
    return f != nullptr && f->make_stream_writer != nullptr;
}

}  // namespace io
}  // namespace tttrlib

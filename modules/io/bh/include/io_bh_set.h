// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_BECKERHICKL_SET_H
#define TTTRLIB_IO_BECKERHICKL_SET_H

/*!
 * \file io_bh_set.h
 * \brief The whole Becker & Hickl ".set" sidecar, not the five tags a photon
 *        reader needs.
 *
 * `read_bh_set_file` (io_bh.h) extracts SP_IMG_X, SP_IMG_Y, SP_PIX_CLK and,
 * for SPC-QC, SP_TAC_R and SP_ADC_RE, and folds them into a TTTR header. That
 * is the right scope for a photon reader: they are what the 4-byte .spc header
 * cannot carry, and nothing else in the file changes how the records decode.
 *
 * It is also about four per cent of the file. The rest is the hardware
 * configuration the measurement was taken with -- CFD levels, TAC range and
 * gain, sync divider, collection time, dead-time compensation -- and anything
 * that drives an SPC card wants it. Without this, such a caller writes a second
 * parser, which is what happened, and that second parser then does not read the
 * imaging tags, so the same file gets parsed twice by two implementations that
 * each ignore what the other wants.
 *
 * ### What a .set is
 *
 * A binary preamble, then text blocks in CRLF lines:
 *
 * ```
 * *IDENTIFICATION
 *   ID        : SPC Setup Script File
 *   Title     : sample_c10
 * *END
 *
 * *SETUP
 *   SYS_PARA_BEGIN:
 *   #PR [PR_PDEV,I,18]
 *   #SP [SP_TAC_R,F,6.554e-08]
 *   ...
 *   SYS_PARA_END:
 *   TRACE_PARA_BEGIN:
 *   #TR #0 [1,255,1,1,1,1,1,1]0]1,1,1,0]...]
 *   TRACE_PARA_END:
 *   BIN_PARA_BEGIN: <binary from here to the end of the file>
 * ```
 *
 * ### Values stay text
 *
 * A .set is a device configuration file and its types are per-parameter -- `I`,
 * `F`, `B`, `S`, `C` are what the file itself declares, and they do not always
 * mean what they look like. A parser that converts is a parser that is wrong
 * about one field in a hundred and silent about it, so the declared type comes
 * back alongside the text and the interpretation is the caller's.
 */

#include <string>
#include <vector>

namespace tttrlib {
namespace io {

/// One parameter of a ".set" sidecar, as the file spells it.
struct BhSetParameter {
    /// The block it was found in: "IDENTIFICATION", "SYS_PARA", "TRACE_PARA",
    /// "WIND_PARA", or the name of the enclosing `*SECTION` when there is no
    /// `NAME_BEGIN:` marker.
    std::string section;

    /// The two-letter line tag: "SP" system parameter, "PR" printer/plot,
    /// "DI" display, "TR" trace, "WI" window. Empty for the `key : value`
    /// lines of the identification block.
    std::string group;

    /// The parameter name, e.g. "SP_IMG_X". For the trace and window blocks,
    /// which are indexed rather than named, whatever stands before the bracket
    /// -- e.g. "#1 *NO *3".
    std::string name;

    /// The type letter the file declares: I, F, B, S or C. Empty when the line
    /// carries none.
    std::string type;

    /// The value, verbatim and untouched. \see the note on types above.
    std::string value;
};

/*!
 * \brief Parse a ".set" sidecar the caller already has as bytes.
 *
 * Parsing stops at `BIN_PARA_BEGIN:`. Everything after it is a binary blob of
 * window geometry and colours, and running a line parser over binary finds
 * parameters that are not there.
 *
 * Never throws and never fails: a file that is not a .set simply yields no
 * parameters.
 */
std::vector<BhSetParameter> parse_set(const std::string& content);

/// \see parse_set, for a sidecar on disk. Empty if the file cannot be read.
std::vector<BhSetParameter> read_set_file(const std::string& filename);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_BECKERHICKL_SET_H

// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRFormat.h"
#include "TTTRHeaderTypes.h"   // the container and record-type constants

#include <algorithm>
#include <cctype>
#include <mutex>

namespace tttrlib {

namespace {

/*!
 * \brief The built-in formats.
 *
 * Every value here is transcribed from the code it replaces, not invented:
 * names from TTTR::initialize_container_names(), labels and extensions from
 * Registry.cpp's file_container_entries(), record types from
 * valid_container_record_pair(), defaults from
 * default_record_type_for_container(). A test asserts the table still agrees
 * with each of them, so a divergence is a test failure rather than a format
 * that quietly stops being recognised.
 */
std::vector<FileFormat> builtin_formats() {
    std::vector<FileFormat> f;

    FileFormat ptu;
    ptu.name = "PTU";
    ptu.container_type = PQ_PTU_CONTAINER;
    ptu.label = "PicoQuant PTU";
    ptu.extensions = {"ptu"};
    ptu.record_types = {PQ_RECORD_TYPE_HHT2v1, PQ_RECORD_TYPE_HHT3v2,
                        PQ_RECORD_TYPE_HHT2v2, PQ_RECORD_TYPE_HHT3v1,
                        PQ_RECORD_TYPE_PHT2,   PQ_RECORD_TYPE_PHT3,
                        PQ_RECORD_TYPE_GENERIC_T3, PQ_RECORD_TYPE_GENERIC_T2};
    ptu.default_record_type = PQ_RECORD_TYPE_HHT3v2;
    ptu.can_write = true;
    f.push_back(ptu);

    FileFormat ht3 = ptu;
    ht3.name = "HT3";
    ht3.container_type = PQ_HT3_CONTAINER;
    ht3.label = "PicoQuant HT3";
    ht3.extensions = {"ht3"};
    // SF compression exists only for HT3 containers, not for PTU.
    ht3.record_types.push_back(PQ_RECORD_TYPE_SF_HT3);
    f.push_back(ht3);

    FileFormat spc130;
    spc130.name = "SPC-130";
    spc130.container_type = BH_SPC130_CONTAINER;
    spc130.label = "Becker & Hickl SPC-130";
    spc130.extensions = {"spc"};
    spc130.record_types = {BH_RECORD_TYPE_SPC130};
    spc130.default_record_type = BH_RECORD_TYPE_SPC130;
    spc130.can_write = true;
    f.push_back(spc130);

    FileFormat spc256 = spc130;
    spc256.name = "SPC-600_256";
    spc256.container_type = BH_SPC600_256_CONTAINER;
    spc256.label = "Becker & Hickl SPC-600 (256)";
    // Claims ".spc" but was never a candidate for detection: the dispatcher
    // tried only SPC-130 and SPC-QC for that extension. A caller has to name it.
    spc256.detectable = false;
    spc256.record_types = {BH_RECORD_TYPE_SPC600_256};
    spc256.default_record_type = BH_RECORD_TYPE_SPC600_256;
    f.push_back(spc256);

    FileFormat spc4096 = spc130;
    spc4096.name = "SPC-600_4096";
    spc4096.container_type = BH_SPC600_4096_CONTAINER;
    spc4096.label = "Becker & Hickl SPC-600 (4096)";
    spc4096.detectable = false;   // as SPC-600_256
    spc4096.record_types = {BH_RECORD_TYPE_SPC600_4096};
    spc4096.default_record_type = BH_RECORD_TYPE_SPC600_4096;
    f.push_back(spc4096);

    FileFormat hdf5;
    hdf5.name = "PHOTON-HDF5";
    hdf5.container_type = PHOTON_HDF_CONTAINER;
    hdf5.label = "Photon-HDF5";
    hdf5.extensions = {"h5", "hdf5"};
    hdf5.canonical_extension = "hdf5";   // tttrContainerCanonicalExtension() wrote "hdf5"
    // Photon-HDF5 stores decoded arrays, so any record type is acceptable and
    // none is canonical -- empty means "any".
    hdf5.can_write = true;
    f.push_back(hdf5);

    FileFormat cz;
    cz.name = "CZ-RAW";
    cz.container_type = CZ_CONFOCOR3_CONTAINER;
    cz.label = "Zeiss ConfoCor3 raw";
    cz.extensions = {"raw"};
    cz.record_types = {CZ_RECORD_TYPE_CONFOCOR3};
    cz.default_record_type = CZ_RECORD_TYPE_CONFOCOR3;
    cz.can_write = true;
    f.push_back(cz);

    FileFormat sm;
    sm.name = "SM";
    sm.container_type = SM_CONTAINER;
    sm.label = "Single-molecule (SM)";
    sm.extensions = {"sm"};
    sm.record_types = {SM_RECORD_TYPE};
    sm.default_record_type = SM_RECORD_TYPE;
    sm.can_write = true;
    f.push_back(sm);

    FileFormat ps;
    ps.name = "PHOTONS";
    ps.container_type = PS_PHOTONS_CONTAINER;
    ps.label = "Photonscore LINCam";
    ps.extensions = {"photons"};
    ps.can_write = true;
    f.push_back(ps);

    FileFormat qc;
    qc.name = "SPC-QC";
    qc.container_type = BH_SPCQC_CONTAINER;
    qc.label = "Becker & Hickl SPC-QC";
    qc.extensions = {"spc"};
    qc.record_types = {BH_RECORD_TYPE_SPCQC_X04, BH_RECORD_TYPE_SPCQC_X06};
    qc.default_record_type = BH_RECORD_TYPE_SPCQC_X04;
    qc.can_write = true;
    f.push_back(qc);

    FileFormat ttr;
    ttr.name = "BRIGHTEYES-TTR";
    ttr.container_type = BE_TTR_CONTAINER;
    ttr.label = "BrightEyes-TTM raw";
    ttr.extensions = {"ttr"};
    // A bare uint16 stream: no header, no magic, nothing to sniff. It can only
    // ever be reached by extension or by being named, so it takes no part in
    // content detection -- any file at all would "match".
    ttr.detectable = false;
    ttr.record_types = {BE_RECORD_TYPE_TTR};
    ttr.default_record_type = BE_RECORD_TYPE_TTR;
    ttr.can_write = true;
    f.push_back(ttr);

    for (auto& fmt : f) {
        if (fmt.summary.empty()) fmt.summary = "TTTR container: " + fmt.label;
    }
    return f;
}

std::vector<FileFormat>& table() {
    static std::vector<FileFormat> t = builtin_formats();
    return t;
}

std::mutex& table_mutex() {
    static std::mutex m;
    return m;
}

}  // namespace

const std::vector<FileFormat>& IORegistry::formats() {
    return table();
}

const FileFormat* IORegistry::by_name(const std::string& name) {
    for (const auto& f : table()) if (f.name == name) return &f;
    return nullptr;
}

const FileFormat* IORegistry::by_container_type(int container_type) {
    for (const auto& f : table()) if (f.container_type == container_type) return &f;
    return nullptr;
}

std::vector<const FileFormat*> IORegistry::by_extension(const std::string& extension) {
    std::vector<const FileFormat*> out;
    for (const auto& f : table()) if (f.has_extension(extension)) out.push_back(&f);
    std::sort(out.begin(), out.end(), [](const FileFormat* a, const FileFormat* b) {
        return a->container_type < b->container_type;
    });
    return out;
}

bool IORegistry::set_sniffer(const std::string& name,
                             bool (*sniff)(const std::string&)) {
    std::lock_guard<std::mutex> guard(table_mutex());
    for (auto& f : table()) {
        if (f.name == name) { f.sniff = sniff; return true; }
    }
    return false;
}

std::string IORegistry::extension_of(const std::string& filename) {
    const auto dot = filename.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

int IORegistry::container_type_from_extension(const std::string& filename) {
    const auto candidates = by_extension(extension_of(filename));
    return candidates.empty() ? -1 : candidates.front()->container_type;
}

int IORegistry::infer_container_type(const std::string& filename) {
    const std::string ext = extension_of(filename);
    const auto claimants = by_extension(ext);
    for (const FileFormat* f : claimants) {
        if (!f->detectable) continue;          // never identified from a file
        if (f->sniff == nullptr) return f->container_type;   // extension is enough
        if (f->sniff(filename)) return f->container_type;
    }

    // If some format claims this extension, stop here. Its sniffers declined,
    // and that is an answer: a ".spc" whose contents are not Becker & Hickl is
    // unrecognised, not an invitation to try everything else.
    //
    // This matters most for formats that cannot be sniffed at all. A .ttr is a
    // bare uint16 stream, so it is registered as not detectable -- and probing
    // the other sniffers against one found that Becker & Hickl's SPC-QC
    // structural check accepts it, reporting a BrightEyes file as SPC-QC.
    if (!claimants.empty()) return -1;

    // Nothing claims the extension. Now ask every format that can identify
    // itself from bytes.
    //
    // The extension is a hint, not the answer: a correctly formatted file with
    // an unexpected name -- or no extension at all -- was previously
    // undetectable, however unambiguous its contents. This costs a few header
    // reads and only ever turns a -1 into an answer, because a format that
    // claimed the extension has already been tried and declined above.
    //
    // Only formats with a real sniffer take part. That is what keeps the two
    // deliberate exceptions intact: "SM" is accepted on its extension alone and
    // must not start matching arbitrary files, and the SPC-600 variants are not
    // identifiable from content at all.
    for (const FileFormat& f : formats()) {
        if (!f.detectable || f.sniff == nullptr) continue;
        if (f.sniff(filename)) return f.container_type;
    }
    return -1;
}

bool IORegistry::add(const FileFormat& format) {
    std::lock_guard<std::mutex> guard(table_mutex());
    for (const auto& f : table()) {
        if (f.name == format.name) return false;              // refused, not shadowed
        if (f.container_type == format.container_type) return false;
    }
    table().push_back(format);
    std::sort(table().begin(), table().end(),
              [](const FileFormat& a, const FileFormat& b) {
                  return a.container_type < b.container_type;
              });
    return true;
}

}  // namespace tttrlib

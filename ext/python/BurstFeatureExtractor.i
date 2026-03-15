%{
#include "BurstFeatureExtractor.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

// BurstFeatureExtractor is included in the main tttrlib module; no separate %module here.
// Rely on shared STL and numpy typemaps from misc_types.i included by tttrlib.i.

%include "BurstFeatureExtractor.h"

%extend tttrlib::BurstFeatureExtractor {
    // Convenience constructor that accepts a shared_ptr<BurstFilter>
    BurstFeatureExtractor(std::shared_ptr<tttrlib::BurstFilter> bf) {
        if (!bf) {
            SWIG_exception_fail(SWIG_ValueError, "BurstFilter is null");
        }
        return new tttrlib::BurstFeatureExtractor(*bf);
        fail:
        return (tttrlib::BurstFeatureExtractor*)0;
    }

    std::string to_json_string() const {
        json j;
        // Burst properties
        auto props = $self->get_burst_properties();
        j["burst_properties"] = props; // vector<vector<double>> serializes naturally
        // Channel photons per burst
        auto ch_ph = $self->get_burst_channel_photons();
        j["channel_photons"] = ch_ph; // map<string, vector<double>>
        // Channel indices (ragged): map<string, vector<vector<int64_t>>>
        auto ch_idx = $self->get_burst_channel_indices();
        j["channel_indices"] = ch_idx;
        // FRET efficiencies
        auto fret = $self->get_fret_efficiencies();
        j["fret_efficiencies"] = fret;
        return j.dump();
    }

    %pythoncode %{
    @property
    def json(self) -> str:
        return self.to_json_string()

    def to_dict(self):
        """Return BurstFeatureExtractor data as a Python dict parsed from JSON string.
        This keeps the interop boundary to C++ as JSON strings while giving
        convenient Python access to the content.
        """
        import json as _json
        return _json.loads(self.to_json_string())

    @property
    def burst_properties(self):
        """Return per-burst properties as a NumPy structured array.
        Fields:
            - start (int64): start index
            - stop (int64): stop index
            - size (int64): number of photons (counts)
            - duration (float64): duration in seconds
            - count_rate (float64): photons per second
        """
        import numpy as np
        props = self.get_burst_properties()
        dtype = np.dtype([
            ('start', np.int64),
            ('stop', np.int64),
            ('size', np.int64),
            ('duration', np.float64),
            ('count_rate', np.float64),
        ])
        if not props:
            return np.empty(0, dtype=dtype)
        a = np.asarray(props).reshape((-1, 5))
        out = np.empty(a.shape[0], dtype=dtype)
        out['start'] = a[:, 0].astype(np.int64, copy=False)
        out['stop'] = a[:, 1].astype(np.int64, copy=False)
        out['size'] = a[:, 2].astype(np.int64, copy=False)
        out['duration'] = a[:, 3].astype(np.float64, copy=False)
        out['count_rate'] = a[:, 4].astype(np.float64, copy=False)
        return out

    @property
    def fret_efficiencies(self):
        """Return FRET efficiencies as a NumPy array of shape (n_bursts,)."""
        import numpy as np
        frets = self.get_fret_efficiencies()
        return np.asarray(frets, dtype=float) if frets else np.empty((0,), dtype=float)

    def channel_photons(self):
        """Return a dict[str, np.ndarray[int64]] with per-burst photon counts per channel.
        Counts are integers. Uses the JSON path to avoid SWIG map proxy differences.
        """
        import numpy as np
        import json as _json
        obj = _json.loads(self.to_json_string())
        ch = obj.get("channel_photons", {})
        return {str(k): np.asarray(v, dtype=np.int64) for k, v in ch.items()} if ch else {}

    def channel_indices(self):
        """Return a dict[str, list[list[int64]]] with per-channel photon indices per burst.
        Indices are ragged lists of lists. Uses the JSON path to avoid SWIG map proxy differences.
        """
        import json as _json
        obj = _json.loads(self.to_json_string())
        ch = obj.get("channel_indices", {})
        # Convert to plain Python lists (ragged structure)
        return {str(k): [[int(idx) for idx in burst_indices] for burst_indices in v] for k, v in ch.items()} if ch else {}
    %}
}

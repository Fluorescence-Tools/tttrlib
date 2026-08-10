import os
import tempfile
import json
import pytest
import numpy as np
import tttrlib

def test_provenance_based_reconstruction_from_pto():
    """Verify that a single-molecule burst analysis can be 100% reconstructed

    from its embedded .pto provenance graph, settings_json, and photon stream.
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        pto_path = os.path.join(tmpdir, "test_reconstruction.pto")
        
        # 1. Create synthetic photon stream
        config_path = "examples/simulation/configs/alex.json"
        with open(config_path, "r") as f:
            config_text = f.read()
        sim = tttrlib.SimEngine.from_json(config_text)
        sim.run()
        tttr_data = sim.to_tttr(0.01, 2)
        assert tttr_data.size() > 0

        # 2. Perform burst selection with known settings
        L_param = 20
        m_param = 5
        T_param = 0.0005
        
        bf = tttrlib.BurstFilter(tttr_data)
        bf.find_bursts()
        n_bursts_orig = bf.get_burst_count()
        props_orig = bf.get_all_burst_properties()
        assert n_bursts_orig > 0

        # 3. Create .pto container and write photon stream & primary burst store
        pto = tttrlib.PtoFile()
        assert pto.create(pto_path, "Reconstruction Test Container")
        
        # Add profile tags
        tag_p = tttrlib.PtoTag(); tag_p.target = 0; tag_p.name = "_mmfdb_container.profile"; tag_p.type = tttrlib.PtoType_Text; tag_p.text = "PTO.MFDB"; pto.add_tag(tag_p)
        tag_pv = tttrlib.PtoTag(); tag_pv.target = 0; tag_pv.name = "_mmfdb_container.profile_version"; tag_pv.type = tttrlib.PtoType_Text; tag_pv.text = "1.1"; pto.add_tag(tag_pv)

        # Write raw photon stream payload
        stream_temp = os.path.join(tmpdir, "stream.sm")
        tttr_data.write(stream_temp)
        stream_uid = pto.add_file("tttr_photon_stream", "sm", "data/stream.sm", stream_temp)
        assert stream_uid != 0

        # Write primary burst table store
        store = tttrlib.DataStore("bursts")
        store.set_n_rows(n_bursts_orig)
        
        st_arr = props_orig[:, 0].astype(np.int64)
        sp_arr = props_orig[:, 1].astype(np.int64)
        cnt_arr = props_orig[:, 2].astype(np.int64)
        dur_arr = props_orig[:, 3].astype(np.float64) * 1000.0
        rate_arr = props_orig[:, 4].astype(np.float64)

        store.add("First Photon", st_arr)
        store.add("Last Photon", sp_arr)
        store.add("Number of Photons", cnt_arr)
        store.add("Duration (ms)", dur_arr)
        store.add("Count Rate (KHz)", rate_arr)

        burst_uid = tttrlib.pto_add_store(pto, "burst_table", "burstwise/bi4_bur/stream.bur", store)
        assert burst_uid != 0

        # Add artifact & operation tags + processing settings JSON
        settings_dict = {
            "method": "sliding_window",
            "min_photons": L_param,
            "rate_window": m_param,
            "time_separation": T_param,
            "routing_channels": [0, 1]
        }
        settings_json_str = json.dumps(settings_dict)

        t1 = tttrlib.PtoTag(); t1.target = burst_uid; t1.name = "_mmfdb_artifact.row_grain"; t1.type = tttrlib.PtoType_Text; t1.text = "burst"; pto.add_tag(t1)
        t2 = tttrlib.PtoTag(); t2.target = burst_uid; t2.name = "_mmfdb_artifact.data_format"; t2.type = tttrlib.PtoType_Text; t2.text = "bur"; pto.add_tag(t2)
        t3 = tttrlib.PtoTag(); t3.target = burst_uid; t3.name = "_mmfdb_operation.operation_type"; t3.type = tttrlib.PtoType_Text; t3.text = "burst_selection"; pto.add_tag(t3)
        t4 = tttrlib.PtoTag(); t4.target = burst_uid; t4.name = "_mmfdb_operation.settings_json"; t4.type = tttrlib.PtoType_Text; t4.text = settings_json_str; pto.add_tag(t4)
        t5 = tttrlib.PtoTag(); t5.target = burst_uid; t5.name = "_mmfdb_edge.source_uid"; t5.type = tttrlib.PtoType_UID; t5.u = stream_uid; pto.add_tag(t5)
        t6 = tttrlib.PtoTag(); t6.target = burst_uid; t6.name = "_mmfdb_edge.source_node_id"; t6.type = tttrlib.PtoType_UID; t6.u = stream_uid; pto.add_tag(t6)
        t7 = tttrlib.PtoTag(); t7.target = burst_uid; t7.name = "_mmfdb_edge.relationship_type"; t7.type = tttrlib.PtoType_Text; t7.text = "derived_from"; pto.add_tag(t7)

        assert pto.commit()
        pto.close()

        # 4. Perform provenance-based reconstruction
        import sys
        sys.path.insert(0, "/Users/tpeulen/dev/chisurf")
        from chisurf.core.fio.pto_reconstruct import reconstruct_analysis_from_pto

        recon_result = reconstruct_analysis_from_pto(pto_path)

        assert recon_result["is_exact_match"] is True
        assert recon_result["n_bursts_original"] == n_bursts_orig
        assert recon_result["n_bursts_reconstructed"] == n_bursts_orig
        assert recon_result["settings_used"]["min_photons"] == L_param
        assert recon_result["settings_used"]["rate_window"] == m_param
        assert recon_result["source_uid"] == stream_uid
        assert "burst_selection" in recon_result["lineage_text"]

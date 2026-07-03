"""Round-trip tests for TIFF I/O (tttrlib.imread / tttrlib.imwrite).

Self-contained: everything is written to and read back from tmp_path, so no
external test data is required. Exercises every supported dtype, 2-D and 3-D
(multi-page) arrays, and the available lossless compressors.
"""
from __future__ import annotations

import numpy as np
import pytest

import tttrlib


SUPPORTED_DTYPES = [
    np.uint8, np.uint16, np.uint32,
    np.int32,
    np.float32, np.float64,
]

COMPRESSIONS = ["none", "lzw", "packbits"]


def _sample(dtype, shape):
    rng = np.random.default_rng(1234)
    if np.issubdtype(dtype, np.floating):
        return (rng.standard_normal(shape) * 100.0).astype(dtype)
    info = np.iinfo(dtype)
    hi = min(info.max, 60000)  # keep values reasonable but span the range
    return rng.integers(info.min, hi, size=shape, endpoint=True).astype(dtype)


@pytest.mark.parametrize("dtype", SUPPORTED_DTYPES)
def test_roundtrip_2d(tmp_path, dtype):
    path = str(tmp_path / "img2d.tif")
    arr = _sample(dtype, (17, 23))
    tttrlib.imwrite(path, arr)
    back = tttrlib.imread(path)
    assert back.shape == arr.shape           # single page -> squeezed to 2-D
    assert back.dtype == np.dtype(dtype)
    np.testing.assert_array_equal(back, arr)


@pytest.mark.parametrize("dtype", SUPPORTED_DTYPES)
def test_roundtrip_3d_stack(tmp_path, dtype):
    path = str(tmp_path / "img3d.tif")
    arr = _sample(dtype, (5, 9, 7))
    tttrlib.imwrite(path, arr)
    back = tttrlib.imread(path)
    assert back.shape == arr.shape           # multi-page -> 3-D
    assert back.dtype == np.dtype(dtype)
    np.testing.assert_array_equal(back, arr)


@pytest.mark.parametrize("compression", COMPRESSIONS)
def test_compressions(tmp_path, compression):
    path = str(tmp_path / f"img_{compression}.tif")
    arr = _sample(np.uint16, (3, 12, 15))
    tttrlib.imwrite(path, arr, compression=compression)
    back = tttrlib.imread(path)
    np.testing.assert_array_equal(back, arr)


def test_info_and_dtype(tmp_path):
    path = str(tmp_path / "info.tif")
    arr = _sample(np.float32, (4, 6, 8))
    tttrlib.imwrite(path, arr)
    info = tttrlib.tiff_info(path)
    assert (info.n_frames, info.height, info.width) == (4, 6, 8)
    assert tttrlib.tiff_dtype(path) == "float32"


def test_single_page_no_squeeze(tmp_path):
    path = str(tmp_path / "one.tif")
    arr = _sample(np.uint8, (10, 11))
    tttrlib.imwrite(path, arr)
    back = tttrlib.imread(path, squeeze=False)
    assert back.shape == (1, 10, 11)


def test_non_contiguous_input(tmp_path):
    # A transposed (non-C-contiguous) view must still round-trip correctly.
    path = str(tmp_path / "nc.tif")
    arr = _sample(np.uint16, (8, 12)).T
    assert not arr.flags["C_CONTIGUOUS"]
    tttrlib.imwrite(path, arr)
    back = tttrlib.imread(path)
    np.testing.assert_array_equal(back, arr)


def test_dtype_promotion_on_write(tmp_path):
    # int16 has no direct writer -> promoted to int32 on disk, read back as int32.
    path = str(tmp_path / "prom.tif")
    arr = _sample(np.int16, (6, 6))
    tttrlib.imwrite(path, arr)
    back = tttrlib.imread(path)
    assert back.dtype == np.int32
    np.testing.assert_array_equal(back.astype(np.int16), arr)


def test_pathlib_path_accepted(tmp_path):
    # imread/imwrite accept os.PathLike (pathlib.Path), not only str.
    path = tmp_path / "pathlike.tif"
    arr = _sample(np.uint16, (7, 9))
    tttrlib.imwrite(path, arr)
    np.testing.assert_array_equal(tttrlib.imread(path), arr)


def test_read_missing_file_raises(tmp_path):
    with pytest.raises(Exception):
        tttrlib.imread(str(tmp_path / "does_not_exist.tif"))


def test_imwrite_rejects_4d(tmp_path):
    path = str(tmp_path / "bad.tif")
    with pytest.raises(ValueError):
        tttrlib.imwrite(path, np.zeros((2, 3, 4, 5), dtype=np.uint8))

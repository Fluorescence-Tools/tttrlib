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


def test_imwrite_rejects_1d(tmp_path):
    path = str(tmp_path / "bad.tif")
    with pytest.raises(ValueError):
        tttrlib.imwrite(path, np.zeros(8, dtype=np.uint8))


# ---- ImageJ hyperstack metadata --------------------------------------------
# A TIFF is a flat page sequence, so six pages cannot say on their own whether
# they are six frames or two frames in three colours. These tests pin the
# axis bookkeeping that carries the difference through a round-trip.


def test_plain_stack_has_unlabelled_page_axis(tmp_path):
    path = str(tmp_path / "plain.tif")
    tttrlib.imwrite(path, _sample(np.uint16, (5, 8, 9)))
    meta = tttrlib.tiff_metadata(path)
    assert meta["axes"] == "IYX"
    assert meta["shape"] == (5, 8, 9)
    assert meta["dtype"] == "uint16"


def test_single_page_axes(tmp_path):
    path = str(tmp_path / "single.tif")
    tttrlib.imwrite(path, _sample(np.float32, (8, 9)))
    assert tttrlib.tiff_metadata(path)["axes"] == "YX"


@pytest.mark.parametrize(
    "axes, shape",
    [
        ("CYX", (3, 8, 9)),
        ("TYX", (4, 8, 9)),
        ("TCYX", (2, 3, 8, 9)),
        ("ZCYX", (3, 2, 8, 9)),
        ("TZCYX", (2, 3, 4, 8, 9)),
    ],
)
def test_hyperstack_round_trip(tmp_path, axes, shape):
    path = str(tmp_path / "hs.tif")
    arr = _sample(np.float32, shape)
    tttrlib.imwrite(path, arr, axes=axes)
    meta = tttrlib.tiff_metadata(path)
    assert meta["axes"] == axes
    assert meta["shape"] == shape
    back = tttrlib.imread(path)
    assert back.shape == shape
    np.testing.assert_array_equal(back, arr)


def test_hyperstack_page_order_is_channel_fastest(tmp_path):
    # ImageJ's page order is fixed: channel varies fastest, then slice, then
    # frame. A reader that reshaped in any other order would still return the
    # right *shape*, so check which pixels landed where.
    path = str(tmp_path / "order.tif")
    arr = np.arange(2 * 3 * 4 * 5, dtype=np.float32).reshape(2, 3, 4, 5)
    tttrlib.imwrite(path, arr, axes="TCYX")
    flat = tttrlib._tiff_read_f32(path)
    assert flat.shape == (6, 4, 5)
    np.testing.assert_array_equal(flat[1], arr[0, 1])  # 2nd page = t0, c1
    np.testing.assert_array_equal(flat[3], arr[1, 0])  # 4th page = t1, c0


def test_more_than_3d_defaults_to_trailing_axis_labels(tmp_path):
    path = str(tmp_path / "nd.tif")
    arr = _sample(np.uint16, (2, 3, 8, 9))
    tttrlib.imwrite(path, arr)  # no axes= given
    assert tttrlib.tiff_metadata(path)["axes"] == "ZCYX"
    np.testing.assert_array_equal(tttrlib.imread(path), arr)


@pytest.mark.parametrize(
    "axes, shape",
    [
        ("TCY", (2, 3, 8, 9)),    # does not end in the image plane
        ("TCYX", (3, 8, 9)),      # length disagrees with ndim
        ("SYX", (3, 8, 9)),       # S is not a page label
    ],
)
def test_imwrite_rejects_bad_axes(tmp_path, axes, shape):
    with pytest.raises(ValueError):
        tttrlib.imwrite(str(tmp_path / "bad.tif"), np.zeros(shape, np.uint8), axes=axes)


def test_description_disagreeing_with_page_count_is_ignored(tmp_path):
    # A description claiming a grid that does not multiply out to the pages on
    # disk is stale or truncated. Reshaping to it would silently scramble the
    # stack, so the file must read back flat instead.
    path = str(tmp_path / "stale.tif")
    arr = _sample(np.uint16, (5, 8, 9))
    tttrlib._tiff_write_u16(path, arr, "lzw", "ImageJ=1.54f\nimages=6\nchannels=3\nframes=2\n")
    assert tttrlib.tiff_metadata(path)["axes"] == "IYX"
    np.testing.assert_array_equal(tttrlib.imread(path), arr)


def test_free_text_description_is_preserved_and_not_parsed(tmp_path):
    # ImageDescription is also used for free-form text; it must not be mistaken
    # for a layout, and it must survive being read back.
    path = str(tmp_path / "text.tif")
    arr = _sample(np.uint8, (3, 8, 9))
    tttrlib._tiff_write_u8(path, arr, "lzw", "acquired on setup B")
    meta = tttrlib.tiff_metadata(path)
    assert meta["description"] == "acquired on setup B"
    assert meta["axes"] == "IYX"


def test_voxel_size_metadata_round_trips(tmp_path):
    # A z-stack is only a volume if the reader knows the voxel size: x/y live in
    # the resolution tags, z and the unit name in the description. Writing one
    # without the other gives a stack that measures wrong in ImageJ.
    path = str(tmp_path / "voxel.tif")
    arr = _sample(np.float32, (5, 8, 9))
    tttrlib.imwrite(path, arr, axes="ZYX", resolution=(25.0, 25.0),
                    metadata={"spacing": 0.1, "unit": "um"})
    description = tttrlib.tiff_metadata(path)["description"]
    assert "spacing=0.1" in description
    assert "unit=um" in description
    assert tttrlib.tiff_metadata(path)["axes"] == "ZYX"
    np.testing.assert_array_equal(tttrlib.imread(path), arr)


def test_metadata_cannot_override_derived_keys(tmp_path):
    # channels/slices/frames come from the array; a caller-supplied value that
    # disagreed would make the file unreadable by its own reader.
    with pytest.raises(ValueError):
        tttrlib.imwrite(str(tmp_path / "bad.tif"), np.zeros((2, 4, 5), np.uint8),
                        axes="ZYX", metadata={"slices": 7})


def test_squeeze_false_keeps_single_page_3d(tmp_path):
    path = str(tmp_path / "one.tif")
    tttrlib.imwrite(path, _sample(np.uint16, (8, 9)))
    assert tttrlib.imread(path, squeeze=False).shape == (1, 8, 9)

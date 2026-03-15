"""
Generic ISM reconstruction pipeline using tttrlib only.

This script loads a PTU file via tttrlib, constructs a CLSMImage with
split_by_channel=True (so that detector elements are exposed as frames),
and performs Adaptive Pixel Reassignment (APR) and optionally Focus-ISM
background rejection using tttrlib.CLSMISM. No BrightEyes dependencies.

Example:
    python examples/ism/ism_reconstruction.py --file <path-to>.ptu \
        --focus --save-fig ism_result.png --save-npz ism_result.npz
"""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.offsetbox import AnchoredText
import click

try:
    import tttrlib  # type: ignore
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "tttrlib is required. Please ensure the Python bindings are built and available."
    ) from exc

# Default PTU file — set TTTRLIB_DATA env var to point to your data root
_DATA_ROOT = Path(os.environ.get("TTTRLIB_DATA", "."))
DEFAULT_PTU = str(
    _DATA_ROOT / "imaging/pq/Luminosa_SPAD_Array/100x_ISM_no_PH_485nm_lin_pol_Silvio_6spec_no5_25us_1.ptu"
)


def _load_clsm_image(ptu_file: str, *, n_det: int | None) -> tuple[tttrlib.CLSMImage, dict]:
    """Load a CLSMImage from a PTU file and return metadata.
    The image is constructed with split_by_channel=True so that the intensity
    stack has shape (n_det, n_lines, n_pixel).
    """
    print(f"Loading TTTR: {ptu_file}")
    tttr = tttrlib.TTTR(ptu_file, "PTU")
    print("Constructing CLSMImage (split_by_channel=True, fill=True)")
    img = tttrlib.CLSMImage(tttr_data=tttr, split_by_channel=True, fill=True)

    intensity = np.asarray(img.intensity)
    if intensity.ndim != 3:
        raise RuntimeError(
            f"Unexpected intensity shape {intensity.shape}; expected (det, lines, pixels)."
        )
    total_det, n_lines, n_pixel = intensity.shape
    use_det = total_det if (n_det is None or n_det <= 0 or n_det > total_det) else int(n_det)
    meta = {"n_det": use_det, "n_lines": int(n_lines), "n_pixel": int(n_pixel)}
    print(
        f"Image shape (detectors, lines, pixel): {meta['n_det']} x {meta['n_lines']} x {meta['n_pixel']}"
    )
    return img, meta


def _imshow(img2d: np.ndarray, title: str, *, subplot: Optional[int] = None):
    if subplot is not None:
        plt.subplot(subplot)
    plt.imshow(img2d, cmap="gray")
    plt.title(title)
    plt.axis("off")


def _save_image(path: str, image: np.ndarray, title: str = "") -> None:
    if not path:
        return
    try:
        # Scale to min/max for display
        vmin = float(np.min(image))
        vmax = float(np.max(image))
        if vmax <= vmin:
            vmax = vmin + 1.0
        plt.figure(figsize=(5, 5))
        plt.imshow(image, cmap="magma", vmin=vmin, vmax=vmax)
        if title:
            plt.title(title)
        plt.axis("off")
        plt.tight_layout()
        plt.savefig(path, dpi=150)
        plt.close()
        print(f"Saved {title or 'image'} to {path}")
    except Exception as exc:
        print(f"Warning: could not save image to {path}: {exc}")


def _split_checkerboard(img: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    img = np.asarray(img)
    y, x = np.indices(img.shape)
    mask = (x + y) % 2 == 0
    a = np.zeros_like(img)
    b = np.zeros_like(img)
    a[mask] = img[mask]
    b[~mask] = img[~mask]
    return a, b


def _compute_basic_frc(image: np.ndarray, *, bin_width: float = 2.0, threshold: float = (1.0 / 7.0)) -> None:
    try:
        a, b = _split_checkerboard(np.asarray(image, dtype=np.float64))
        density, bin_edges = tttrlib.CLSMImage.compute_frc(a, b, bin_width=bin_width, apply_hann=True)
        if len(bin_edges) < 2:
            print("FRC: insufficient bins to compute frequency.")
            return
        centers = 0.5 * (bin_edges[:-1] + bin_edges[1:])
        max_dim = float(max(image.shape))
        if max_dim <= 0:
            print("FRC: invalid image shape for frequency scaling.")
            return
        freq = centers / max_dim
        density = np.asarray(density, dtype=np.float64)
        # Estimate crossing at threshold for a crude resolution in px
        res_px = None
        below = np.where(density < threshold)[0]
        if below.size:
            idx = int(below[0])
            if idx > 0:
                x0, x1 = float(freq[idx - 1]), float(freq[idx])
                y0, y1 = float(density[idx - 1]), float(density[idx])
                if x1 != x0:
                    slope = (y1 - y0) / (x1 - x0)
                    intercept = y0 - slope * x0
                    if slope != 0:
                        cross = (threshold - intercept) / slope
                        if cross > 0:
                            res_px = 1.0 / cross
        if res_px is not None:
            print(f"FRC: estimated resolution ≈ {res_px:.3f} px (threshold {threshold:g})")
        else:
            print("FRC: could not estimate a resolution crossing.")
    except Exception as exc:
        print(f"FRC computation failed: {exc}")

@click.command()
@click.option("--file", "file_path", type=click.Path(exists=True, dir_okay=False),
              default=DEFAULT_PTU, show_default=True,
              help="Path to the PTU file.")
@click.option("--usf", default=10, show_default=True, type=int, help="Upsampling factor for APR.")
@click.option("--ref-idx", default=-1, show_default=True, type=int,
              help="Reference detector index for APR (-1: auto).")
@click.option("--filter-sigma", default=0.0, show_default=True, type=float,
              help="Gaussian filter sigma (px) for APR registration (0: off).")
@click.option("--focus/--no-focus", default=False, show_default=True,
              help="Apply Focus-ISM background rejection after APR.")
@click.option("--sigma-bound", default=2.0, show_default=True, type=float,
              help="Focus-ISM sigma_bound parameter.")
@click.option("--threshold", default=0.1, show_default=True, type=float,
              help="Focus-ISM threshold parameter.")
@click.option("--calibration-size", default=10, show_default=True, type=int,
              help="Calibration size (frames) for Focus-ISM.")
@click.option("--nz", default=10, show_default=True, type=int, help="z-upsample factor for APR/Focus-ISM.")
@click.option("--n-det", default=-1, show_default=True, type=int,
              help="Number of detector elements to use (-1: use all).")
@click.option("--save-fig", default="", show_default=True, type=str,
              help="Optional path to save a PNG of the result.")
@click.option("--save-npz", default="", show_default=True, type=str,
              help="Optional path to save a NPZ with results.")
@click.option("--no-show", is_flag=True, default=False, show_default=True,
              help="Do not display interactive figures.")
@click.option("--focus-signal-img", default="", show_default=True, type=str,
              help="Optional path to save the Focus-ISM foreground (signal) image (PNG).")
@click.option("--focus-background-img", default="", show_default=True, type=str,
              help="Optional path to save the Focus-ISM background image (PNG).")
@click.option("--frc", is_flag=True, default=False, show_default=True,
              help="Compute a basic Fourier Ring Correlation (FRC) estimate for APR and Focus-ISM images.")
def main(file_path: str, usf: int, ref_idx: int, filter_sigma: float,
         focus: bool, sigma_bound: float, threshold: float, calibration_size: int,
         nz: int, n_det: int, save_fig: str, save_npz: str, no_show: bool,
         focus_signal_img: str, focus_background_img: str, frc: bool):
    """Run ISM reconstruction from a PTU file using tttrlib only."""
    img, meta = _load_clsm_image(file_path, n_det=n_det)

    # Prepare detector cube (D, H, W) as float64 for CLSMISM entry points
    intensity = np.asarray(img.intensity, dtype=np.float64)
    det_count = int(meta["n_det"]) if "n_det" in meta else int(intensity.shape[0])
    detector_cube = np.ascontiguousarray(intensity[:det_count], dtype=np.float64)
    print(f"Detector cube shape for reconstruction: {detector_cube.shape}")

    # APR reconstruction on raw arrays (avoid passing CLSMImage to wrapper)
    print("Running APR reconstruction...")
    apr = tttrlib.CLSMISM.apr_reconstruction(
        detector_cube,
        channels_last=False,
        usf=int(usf),
        ref_idx=int(ref_idx),
        filter_sigma=float(filter_sigma),
        nz=int(nz),
        n_det=det_count,
    )
    # Normalize APR output to 2-D image for display/saving
    apr = apr[0] if isinstance(apr, np.ndarray) and apr.ndim == 3 else apr

    focus_signal = None
    focus_background = None
    if focus:
        print("Running Focus-ISM background rejection...")
        focus_result = tttrlib.CLSMISM.focus_reconstruction(
            detector_cube,
            channels_last=False,
            sigma_bound=float(sigma_bound),
            threshold=float(threshold),
            calibration_size=int(calibration_size),
            parallelize=False,
            nz=int(nz),
            n_det=det_count,
            detector_coords=None,
        )
        # Unify focus outputs (tuple or ndarray)
        if isinstance(focus_result, (tuple, list)):
            focus_signal = focus_result[0] if len(focus_result) > 0 else None
            focus_background = focus_result[1] if len(focus_result) > 1 else None
        elif isinstance(focus_result, np.ndarray):
            if focus_result.ndim == 3 and focus_result.shape[0] >= 1:
                focus_signal = focus_result[0]
                focus_background = focus_result[1] if focus_result.shape[0] > 1 else None
            else:
                focus_signal = focus_result
                focus_background = None
        else:
            focus_signal = None
            focus_background = None

        # Save focus images if requested
        if isinstance(focus_signal, np.ndarray) and focus_signal.size:
            if focus_signal_img:
                _save_image(focus_signal_img, focus_signal, "Focus-ISM (signal)")
            if focus_background_img and isinstance(focus_background, np.ndarray) and focus_background.size:
                _save_image(focus_background_img, focus_background, "Focus-ISM (background)")

    # Visualize with optional FRC row
    if not no_show or save_fig:
        images = [apr]
        titles = ["APR reconstruction"]
        if focus and isinstance(focus_signal, np.ndarray):
            images.append(focus_signal)
            titles.append("Focus-ISM (signal)")
            if isinstance(focus_background, np.ndarray):
                images.append(focus_background)
                titles.append("Focus-ISM (background)")

        rows = 2 if frc else 1
        cols = len(images)
        fig, axes = plt.subplots(rows, cols, figsize=(4.5 * cols, 4.5 * rows))
        axes = np.atleast_2d(axes)

        # Top row: intensity images
        intensity_axes = [None] * cols
        for j, (im, title) in enumerate(zip(images, titles)):
            ax = axes[0, j]
            intensity_axes[j] = ax
            vmin = float(np.min(im))
            vmax = float(np.max(im))
            if vmax <= vmin:
                vmax = vmin + 1.0
            m = ax.imshow(im, cmap="magma", vmin=vmin, vmax=vmax)
            ax.set_title(title)
            ax.set_xticks([])
            ax.set_yticks([])
            cbar = fig.colorbar(m, ax=ax, fraction=0.046, pad=0.04)
            cbar.ax.set_ylabel("counts", rotation=270, labelpad=12)
        # Link top-row image axes for synchronized zoom/pan
        if len(intensity_axes) > 1:
            ref_ax = intensity_axes[0]
            for ax in intensity_axes[1:]:
                ax.sharex(ref_ax)
                ax.sharey(ref_ax)

        # Bottom row: FRC curves (interactive, update on zoom/pan)
        frc_handles = [None] * cols
        baseline_results = [None] * cols
        if frc:
            legend_drawn = False
            for j, (im, title) in enumerate(zip(images, titles)):
                ax = axes[1, j]
                # Disable navigation on FRC axes to avoid confusing zoom; FRC is recomputed from ROI of top images
                try:
                    ax.set_navigate(False)
                except Exception:
                    pass
                try:
                    res = _compute_basic_frc_arrays(im)
                    baseline_results[j] = res
                    freq = res["freq"]
                    frc_vals = res["frc"]
                    thr = res["threshold"]
                    # Create line objects we can update later
                    raw_line, = ax.plot(freq, frc_vals, ".", alpha=0.6, label=("FRC raw" if not legend_drawn else None))
                    thr_line, = ax.plot(freq, thr, "--", label=("Threshold" if not legend_drawn else None))
                    marker_line, = ax.plot([], [], ":", color="C2", linewidth=2)
                    marker_point, = ax.plot([], [], "o", color="C2")

                    # Resolution inset (AnchoredText)
                    res_text = AnchoredText("", loc="lower left", prop=dict(size=9), frameon=True)
                    ax.add_artist(res_text)

                    # Initialize resolution marker and inset text
                    r0 = None
                    try:
                        r0 = res.get("resolution")
                    except Exception:
                        r0 = None
                    if r0 is not None and r0 > 0 and np.size(freq):
                        xf = 1.0 / float(r0)
                        try:
                            y_interp = float(np.interp(xf, freq, frc_vals)) if np.size(freq) else 0.0
                        except Exception:
                            y_interp = 0.0
                        marker_line.set_data([xf, xf], [-0.05, 1.05])
                        marker_point.set_data([xf], [y_interp])
                        res_text.txt.set_text(f"Res \u2248 {float(r0):.3f} px")
                        ax.set_title(f"{title} FRC")
                    else:
                        res_text.txt.set_text("Res: n/a")

                    ax.set_ylim(-0.05, 1.05)
                    if freq.size:
                        ax.set_xlim(float(freq.min()), float(freq.max()))
                    ax.set_xlabel("Spatial frequency (px^{-1})")
                    ax.set_ylabel("FRC")
                    if not legend_drawn:
                        ax.legend(loc="upper right")
                        legend_drawn = True
                    frc_handles[j] = {
                        "ax": ax,
                        "raw": raw_line,
                        "thr": thr_line,
                        "marker_line": marker_line,
                        "marker_point": marker_point,
                        "res_text": res_text,
                        "title": title,
                    }
                except Exception as exc:
                    ax.text(0.5, 0.5, f"FRC failed: {exc}", ha="center", va="center")
                    ax.set_axis_off()
                    frc_handles[j] = None

            # Helper: extract ROI from current view of top image j
            def _extract_roi(j_idx: int):
                im = images[j_idx]
                ax_img = intensity_axes[j_idx]
                x0, x1 = ax_img.get_xlim()
                y0, y1 = ax_img.get_ylim()
                xmin, xmax = (min(x0, x1), max(x0, x1))
                ymin, ymax = (min(y0, y1), max(y0, y1))
                h, w = im.shape
                col_start = max(0, int(np.floor(xmin + 0.5)))
                col_end   = min(w, int(np.ceil(xmax + 0.5)))
                row_start = max(0, int(np.floor(ymin + 0.5)))
                row_end   = min(h, int(np.ceil(ymax + 0.5)))
                # Enforce minimal ROI size
                if col_end - col_start < 4 or row_end - row_start < 4:
                    return None
                return im[row_start:row_end, col_start:col_end]

            # Update function for all FRC plots
            _updating = {"flag": False}
            def _refresh_frc(event=None):
                if _updating["flag"]:
                    return
                # Only react to interactions on top images (button release or scroll)
                if event is not None:
                    name = getattr(event, "name", "")
                    if name == "scroll_event" and event.inaxes not in intensity_axes:
                        return
                    if name == "button_release_event" and event.inaxes not in intensity_axes:
                        # Allow zoom tool that triggers on button release over image axes only
                        return
                _updating["flag"] = True
                try:
                    for j_idx in range(cols):
                        handles = frc_handles[j_idx]
                        if handles is None:
                            continue
                        roi = _extract_roi(j_idx)
                        res = None
                        if roi is not None:
                            try:
                                res = _compute_basic_frc_arrays(roi)
                            except Exception:
                                res = None
                        if res is None:
                            res = baseline_results[j_idx]
                        if res is None:
                            continue
                        freq = np.asarray(res["freq"], dtype=np.float64)
                        frc_vals = np.asarray(res["frc"], dtype=np.float64)
                        thr = np.asarray(res["threshold"], dtype=np.float64)
                        ax_frc = handles["ax"]
                        handles["raw"].set_data(freq, frc_vals)
                        handles["thr"].set_data(freq, thr)
                        # Update resolution marker
                        try:
                            r = res.get("resolution")
                        except Exception:
                            r = None
                        if r is not None and r > 0 and freq.size:
                            xf = 1.0 / float(r)
                            handles["marker_line"].set_data([xf, xf], [-0.05, 1.05])
                            try:
                                import numpy as _np
                                y_interp = float(_np.interp(xf, freq, frc_vals)) if freq.size else 0.0
                            except Exception:
                                y_interp = 0.0
                            handles["marker_point"].set_data([xf], [y_interp])
                            if "res_text" in handles and handles["res_text"] is not None:
                                handles["res_text"].txt.set_text(f"Res \u2248 {float(r):.3f} px")
                        else:
                            handles["marker_line"].set_data([], [])
                            handles["marker_point"].set_data([], [])
                            if "res_text" in handles and handles["res_text"] is not None:
                                handles["res_text"].txt.set_text("Res: n/a")
                        ax_frc.set_ylim(-0.05, 1.05)
                        if freq.size:
                            ax_frc.set_xlim(float(freq.min()), float(freq.max()))
                        # Append ROI suffix to title if zoomed
                        if roi is not None:
                            ax_frc.set_title(f"{handles['title']} FRC · ROI")
                        else:
                            ax_frc.set_title(f"{handles['title']} FRC")
                    fig.canvas.draw_idle()
                finally:
                    _updating["flag"] = False

            fig.canvas.mpl_connect("button_release_event", _refresh_frc)
            fig.canvas.mpl_connect("scroll_event", _refresh_frc)
            # Also update FRC whenever the image axes limits change (e.g., toolbar zoom/pan)
            try:
                for _ax in intensity_axes:
                    if _ax is not None:
                        _ax.callbacks.connect("xlim_changed", lambda evt_ax: _refresh_frc())
                        _ax.callbacks.connect("ylim_changed", lambda evt_ax: _refresh_frc())
            except Exception:
                pass

        fig.tight_layout()
        if save_fig:
            fig.savefig(save_fig, dpi=200)
        if not no_show:
            plt.show()
        else:
            plt.close(fig)

    # Save data
    if save_npz:
        np.savez_compressed(
            save_npz,
            apr=apr,
            focus_signal=focus_signal,
            focus_background=focus_background,
            meta=meta,
        )
        print("Saved:", save_npz)

    # FRC (basic)
    if frc:
        print("Computing basic FRC for APR image...")
        _compute_basic_frc(apr)
        if focus and isinstance(focus_signal, np.ndarray):
            print("Computing basic FRC for Focus-ISM signal image...")
            _compute_basic_frc(focus_signal)



def _compute_basic_frc_arrays(
    image: np.ndarray,
    *,
    bin_width: float = 2.0,
    threshold: float = (1.0 / 7.0),
    apply_hann: bool = True,
) -> dict:
    """Return basic FRC arrays for plotting: freq, frc, threshold, resolution.

    - Splits the image into two checkerboard halves for self-correlation.
    - Uses tttrlib.CLSMImage.compute_frc for the FRC density and bins.
    - Converts radial bin centers to spatial frequency in px^{-1}.
    - Estimates a crude resolution via linear interpolation at the threshold.
    """
    a, b = _split_checkerboard(np.asarray(image, dtype=np.float64))
    density, bin_edges = tttrlib.CLSMImage.compute_frc(
        a, b, bin_width=float(bin_width), apply_hann=bool(apply_hann)
    )
    # Graceful fallbacks for degenerate cases
    if len(bin_edges) < 2:
        return {
            "freq": np.array([], dtype=np.float64),
            "frc": np.array([], dtype=np.float64),
            "threshold": np.array([], dtype=np.float64),
            "resolution": None,
        }
    centers = 0.5 * (bin_edges[:-1] + bin_edges[1:])
    max_dim = float(max(image.shape))
    if max_dim <= 0:
        return {
            "freq": np.array([], dtype=np.float64),
            "frc": np.array([], dtype=np.float64),
            "threshold": np.array([], dtype=np.float64),
            "resolution": None,
        }
    freq = centers / max_dim
    density = np.asarray(density, dtype=np.float64)
    thr = np.full_like(freq, float(threshold), dtype=np.float64)

    # Estimate resolution in pixels (if possible)
    resolution = None
    try:
        below = np.where(density < float(threshold))[0]
        if below.size:
            idx = int(below[0])
            if idx > 0:
                x0, x1 = float(freq[idx - 1]), float(freq[idx])
                y0, y1 = float(density[idx - 1]), float(density[idx])
                if x1 != x0:
                    slope = (y1 - y0) / (x1 - x0)
                    intercept = y0 - slope * x0
                    if slope != 0:
                        cross = (float(threshold) - intercept) / slope
                        if cross > 0:
                            resolution = 1.0 / cross
    except Exception:
        resolution = None

    return {
        "freq": np.asarray(freq, dtype=np.float64),
        "frc": density,
        "threshold": thr,
        "resolution": resolution,
    }


if __name__ == "__main__":
    main()




import numpy as np
from typing import Dict, Any


def parse_bh_set_file(set_filename: str) -> Dict[str, Any]:
    """Parse BH .set file and return settings dict.

    Parameters
    ----------
    set_filename : str
        Path to the BH .set file

    Returns
    -------
    dict
        Dictionary containing parsed settings:
        - 'n_pixel_per_line': int (from SP_IMG_X)
        - 'n_lines': int (from SP_IMG_Y)
        - 'use_pixel_markers': bool (from SP_PIX_CLK == 1)
    """
    settings = {}
    try:
        # BH files are often Windows-encoded (cp1252)
        with open(set_filename, "r", encoding="cp1252", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("*"):
                    continue
                if "=" in line:
                    parts = line.split("=", 1)
                    key = parts[0].strip()
                    val = parts[1].strip()
                    if key == "SP_IMG_X":
                        settings["n_pixel_per_line"] = int(val)
                    elif key == "SP_IMG_Y":
                        settings["n_lines"] = int(val)
                    elif key == "SP_PIX_CLK":
                        settings["use_pixel_markers"] = int(val) == 1
    except FileNotFoundError:
        raise FileNotFoundError(f"BH set file not found: {set_filename}")
    except Exception:
        # For other errors, we return what we have
        pass
    return settings


def detect_frame1_extra_line(
    tttr, frame_marker: int = 4, line_marker: int = 2, marker_event_type: int = 1
) -> bool:
    """
    Detect if Frame 1 has an extra initialization line.

    For BH SPC data, Frame 1 often has an extra partial line at the start
    that should be skipped for proper alignment. This function compares
    the number of line markers in the first frame vs the second frame.

    Parameters
    ----------
    tttr : tttrlib.TTTR
        TTTR data object
    frame_marker : int
        Routing channel for frame markers (default: 4)
    line_marker : int
        Routing channel for line markers (default: 2)
    marker_event_type : int
        Event type for markers (default: 1)

    Returns
    -------
    bool
        True if Frame 1 has an extra initialization line that should be skipped
    """
    # Get indices of all marker events
    # We use get_selection_by_type if available, otherwise fallback to numpy
    try:
        marker_indices = tttr.get_selection_by_type(marker_event_type)
    except AttributeError:
        marker_indices = np.where(tttr.event_types == marker_event_type)[0]

    if len(marker_indices) == 0:
        return False

    # Get routing channels of these markers
    # tttr.routing_channels returns a numpy array
    routing_channels = tttr.routing_channels[marker_indices]

    # Find positions of frame markers in the marker_indices array
    frame_marker_positions = np.where(routing_channels == frame_marker)[0]

    # We need at least three frame markers to compare two complete frames
    if len(frame_marker_positions) < 3:
        return False

    # Count line markers in Frame 1 (between 1st and 2nd frame marker)
    f1_start = frame_marker_positions[0]
    f1_end = frame_marker_positions[1]
    f1_markers = routing_channels[f1_start:f1_end]
    n_lines_f1 = np.sum(f1_markers == line_marker)

    # Count line markers in Frame 2 (between 2nd and 3rd frame marker)
    f2_start = frame_marker_positions[1]
    f2_end = frame_marker_positions[2]
    f2_markers = routing_channels[f2_start:f2_end]
    n_lines_f2 = np.sum(f2_markers == line_marker)

    # Anomaly: Frame 1 has more lines than Frame 2 (specifically one extra)
    # Based on the plan, Frame 1 has 514 lines and Frame 2 has 513 lines.
    if n_lines_f1 == n_lines_f2 + 1:
        return True

    return False

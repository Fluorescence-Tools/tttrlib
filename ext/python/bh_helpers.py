import numpy as np
from typing import Dict, Any, List


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


def get_adjusted_frame_markers(
    tttr, frame_marker: int = 4, line_marker: int = 2, marker_event_type: int = 1
) -> List[int]:
    """
    Return adjusted frame marker indices that account for
    Frame 1's extra initialization line.

    For BH SPC data, Frame 1 often has an extra partial line at the start
    that should be skipped for proper Y-axis alignment. This function
    returns frame start indices where Frame 1's start is adjusted to
    skip the extra line.

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
    list
        List of adjusted frame start indices. If Frame 1 has an extra line,
        its start index is moved to after the first line marker.
    """
    try:
        marker_indices = tttr.get_selection_by_type(marker_event_type)
    except AttributeError:
        marker_indices = np.where(tttr.event_types == marker_event_type)[0]

    if len(marker_indices) == 0:
        return []

    routing_channels = tttr.routing_channels[marker_indices]
    frame_marker_positions = np.where(routing_channels == frame_marker)[0]

    if len(frame_marker_positions) == 0:
        return []

    frame_marker_indices = marker_indices[frame_marker_positions].tolist()

    if detect_frame1_extra_line(tttr, frame_marker, line_marker, marker_event_type):
        # Frame 1 starts at frame_marker_positions[0]
        # Frame 2 starts at frame_marker_positions[1]
        f1_start_pos = frame_marker_positions[0]
        f1_end_pos = frame_marker_positions[1]

        # Find line markers in Frame 1
        f1_routing = routing_channels[f1_start_pos:f1_end_pos]
        line_marker_rel_pos = np.where(f1_routing == line_marker)[0]

        if len(line_marker_rel_pos) > 0:
            # First line marker index in TTTR
            first_line_marker_idx = marker_indices[
                f1_start_pos + line_marker_rel_pos[0]
            ]
            # Adjust the first frame marker to AFTER this line marker index
            # to skip the extra initialization line
            frame_marker_indices[0] = int(first_line_marker_idx) + 1

    return [int(idx) for idx in frame_marker_indices]


def prepare_bh_clsm_settings(
    tttr,
    set_filename: str = None,
    frame_marker: int = 4,
    line_marker: int = 2,
    marker_event_type: int = 1,
) -> Dict[str, Any]:
    """
    Prepare CLSMSettings for BH SPC data with proper alignment corrections.

    This convenience function:
    1. Parses the .set file (if provided) to get dimensions
    2. Detects Frame 1 anomaly and prepares adjusted settings
    3. Returns a dictionary ready to be used with CLSMSettings

    Parameters
    ----------
    tttr : tttrlib.TTTR
        TTTR data object
    set_filename : str, optional
        Path to BH .set file with scan parameters
    frame_marker : int
        Routing channel for frame markers (default: 4)
    line_marker : int
        Routing channel for line markers (default: 2)
    marker_event_type : int
        Event type for markers (default: 1)

    Returns
    -------
    dict
        Settings dictionary containing:
        - 'n_pixel_per_line': int (from .set file or None)
        - 'n_lines': int (from .set file or None)
        - 'use_pixel_markers': bool
        - 'skip_first_line_frame1': bool (True if Frame 1 has extra line)
        - 'marker_frame_start': list (adjusted frame markers if needed)
    """
    settings = {}
    if set_filename:
        settings.update(parse_bh_set_file(set_filename))

    has_extra = detect_frame1_extra_line(
        tttr, frame_marker, line_marker, marker_event_type
    )
    settings["skip_first_line_frame1"] = has_extra
    settings["marker_frame_start"] = get_adjusted_frame_markers(
        tttr, frame_marker, line_marker, marker_event_type
    )

    return settings

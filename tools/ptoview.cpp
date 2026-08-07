// SPDX-License-Identifier: BSD-3-Clause
#include "pto_read.h"
#include "pto_tui.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cinttypes>
#include <cmath>

enum class ViewTab {
    Summary = 0,
    TimeTrace = 1,
    Decays = 2,
    Objects = 3,
    EBMLTree = 4
};

extern "C" int ptoview_main(int argc, char** argv, const PtoReadFileInfo* info_arg) {
    PtoReadFileInfo local_info;
    const PtoReadFileInfo* info = info_arg;

    if (!info) {
        if (argc < 2) {
            std::cerr << "usage: ptoview <file.pto>" << std::endl;
            return 2;
        }
        if (pto_read_open(argv[1], &local_info) != 0) {
            std::cerr << "error: cannot open container " << argv[1] << std::endl;
            return 1;
        }
        info = &local_info;
    }

    PtoReadInspectionData insp;
    pto_read_get_inspection_data(info->filename, info, &insp);

    pto_tui::App app;
    app.title = std::string("ptoview - ") + info->filename;

    ViewTab current_tab = ViewTab::Summary;
    size_t selected_obj = 0;
    std::string message = "Press 1-5 to switch menu tabs";

    /* Time Trace state */
    int trace_offset = 0;
    int trace_zoom = 1; /* 1 = 1x (fits screen width), 2 = 2x, etc. */
    int trace_cursor = 0;
    bool trace_playing = false;

    /* Decay state */
    int decay_channel_idx = 0;
    bool decay_log_scale = true;

    auto draw = [&]() {
        int width = 80, height = 24;
        pto_tui::Terminal::get_size(width, height);
        if (width < 40) width = 40;
        if (height < 15) height = 15;

        std::cout << "\033[H\033[J"; /* Clear screen */

        /* Top Menu Bar */
        std::stringstream tab_bar;
        tab_bar << " PTOVIEW ";
        tab_bar << (current_tab == ViewTab::Summary   ? " [1:Summary] "   : "  1:Summary  ");
        tab_bar << (current_tab == ViewTab::TimeTrace ? " [2:Time Trace] " : "  2:Time Trace  ");
        tab_bar << (current_tab == ViewTab::Decays    ? " [3:Decays] "    : "  3:Decays  ");
        tab_bar << (current_tab == ViewTab::Objects   ? " [4:Objects] "   : "  4:Objects  ");
        tab_bar << (current_tab == ViewTab::EBMLTree  ? " [5:EBML Tree] " : "  5:EBML Tree  ");

        std::string tb_str = tab_bar.str();
        if ((int)tb_str.length() < width) tb_str.append(width - tb_str.length(), ' ');
        std::cout << "\033[7;1m" << tb_str.substr(0, width) << "\033[0m\n";

        int main_h = height - 3;

        /* TAB 1: SUMMARY */
        if (current_tab == ViewTab::Summary) {
            std::vector<std::string> lines;
            lines.push_back("--- CONTAINER INFORMATION ---");
            lines.push_back(" File:            " + std::string(info->filename));
            lines.push_back(" Title:           " + std::string(info->title[0] ? info->title : "(none)"));
            lines.push_back(" UUID:            " + std::string(info->uuid_hex));
            lines.push_back(" Generation:      " + std::to_string(info->generation));
            lines.push_back(" DocType Version: " + std::to_string(info->doctype_version));

            char sz[32];
            pto_format_bytes(info->file_size, sz, sizeof(sz));
            lines.push_back(" Total Size:      " + std::string(sz) + " (" + std::to_string(info->file_size) + " bytes)");
            lines.push_back(" Objects:         " + std::to_string(info->num_objects));
            lines.push_back(" Tags:            " + std::to_string(info->num_tags));

            lines.push_back("");
            lines.push_back("--- EMBEDDED TTTR METADATA ---");
            std::stringstream ss(insp.metadata_json);
            std::string line;
            while (std::getline(ss, line)) lines.push_back(" " + line);

            if (info->banner[0]) {
                lines.push_back("");
                lines.push_back("--- PTO BANNER ---");
                std::stringstream bss(info->banner);
                while (std::getline(bss, line)) lines.push_back(" " + line);
            }

            for (int r = 0; r < main_h; r++) {
                std::cout << "\033[" << (r + 2) << ";1H";
                std::string txt = (r < (int)lines.size()) ? lines[r] : "";
                if ((int)txt.length() < width) txt.append(width - txt.length(), ' ');
                std::cout << txt.substr(0, width);
            }
        }
        /* TAB 2: DYNAMIC TIME TRACE */
        else if (current_tab == ViewTab::TimeTrace) {
            const auto& tr = insp.trace;
            int graph_w = width - 10;
            int graph_h = main_h - 4;
            if (graph_w < 20) graph_w = 20;
            if (graph_h < 4) graph_h = 4;

            int visible_bins = graph_w / trace_zoom;
            if (visible_bins < 10) visible_bins = 10;
            if (visible_bins > (int)tr.n_bins) visible_bins = tr.n_bins;

            if (trace_playing) {
                trace_offset += 2;
                if (trace_offset + visible_bins >= (int)tr.n_bins) trace_offset = 0;
            }

            if (trace_offset < 0) trace_offset = 0;
            if (trace_offset + visible_bins > (int)tr.n_bins) trace_offset = (int)tr.n_bins - visible_bins;
            if (trace_cursor < trace_offset) trace_cursor = trace_offset;
            if (trace_cursor >= trace_offset + visible_bins) trace_cursor = trace_offset + visible_bins - 1;

            /* Compute stats for visible window */
            uint32_t win_min = 0xFFFFFFFF, win_max = 0;
            uint64_t win_sum = 0;
            for (int b = trace_offset; b < trace_offset + visible_bins; b++) {
                uint32_t c = tr.counts[b];
                if (c < win_min) win_min = c;
                if (c > win_max) win_max = c;
                win_sum += c;
            }
            double win_mean = visible_bins > 0 ? (double)win_sum / visible_bins : 0.0;
            if (win_max == 0) win_max = 1;

            /* Sub-block characters for high-density rendering */
            const char* blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

            std::cout << "\033[2;1H\033[1m MACROTIME PHOTON COUNT TIME TRACE ["
                      << (trace_playing ? "ANIMATING ►" : "PAUSED ❚❚") << "]\033[0m\n";

            /* Draw Graph Rows */
            for (int r = 0; r < graph_h; r++) {
                int row_y = 3 + r;
                std::cout << "\033[" << row_y << ";1H";

                double row_val = win_max * (double)(graph_h - r) / graph_h;
                std::cout << std::setw(7) << (int)row_val << " │";

                for (int col = 0; col < graph_w; col++) {
                    int bin_idx = trace_offset + (col * visible_bins) / graph_w;
                    if (bin_idx >= (int)tr.n_bins) bin_idx = tr.n_bins - 1;

                    uint32_t count = tr.counts[bin_idx];
                    double norm = (double)count / win_max * graph_h;
                    double fill = norm - (graph_h - 1 - r);

                    if (bin_idx == trace_cursor) {
                        std::cout << "\033[7;33m│\033[0m"; /* Cursor bar */
                    } else if (fill <= 0) {
                        std::cout << " ";
                    } else if (fill >= 1.0) {
                        std::cout << "\033[36m█\033[0m";
                    } else {
                        int b_idx = (int)(fill * 8.0);
                        if (b_idx < 0) b_idx = 0;
                        if (b_idx > 8) b_idx = 8;
                        std::cout << "\033[36m" << blocks[b_idx] << "\033[0m";
                    }
                }
            }

            /* Draw X Axis */
            std::cout << "\033[" << (3 + graph_h) << ";1H        └";
            for (int i = 0; i < graph_w; i++) std::cout << "─";
            std::cout << "\n";

            /* Status & Inspector line */
            double cur_time_s = trace_cursor * tr.dt_seconds;
            uint32_t cur_count = (trace_cursor < (int)tr.n_bins) ? tr.counts[trace_cursor] : 0;
            double cur_rate_khz = (tr.dt_seconds > 0) ? (cur_count / tr.dt_seconds) / 1000.0 : 0.0;

            std::cout << "\033[" << (4 + graph_h) << ";1H\033[1;33m CURSOR: " << std::fixed << std::setprecision(3)
                      << cur_time_s << "s (Bin " << trace_cursor << ") │ Count: " << cur_count
                      << " │ Rate: " << std::setprecision(1) << cur_rate_khz << " kHz\033[0m\n";
            std::cout << "\033[" << (5 + graph_h) << ";1H WINDOW: " << trace_offset << ".." << (trace_offset + visible_bins)
                      << " / " << tr.n_bins << " bins │ Zoom: " << trace_zoom << "x │ Min: " << win_min
                      << " │ Max: " << win_max << " │ Mean: " << std::setprecision(1) << win_mean;
        }
        /* TAB 3: DECAYS */
        else if (current_tab == ViewTab::Decays) {
            if (insp.num_decays == 0) {
                std::cout << "\033[2;1H No decay data available.";
            } else {
                if (decay_channel_idx >= (int)insp.num_decays) decay_channel_idx = 0;
                const auto& dec = insp.decays[decay_channel_idx];

                int graph_w = width - 10;
                int graph_h = main_h - 4;
                if (graph_w < 20) graph_w = 20;
                if (graph_h < 4) graph_h = 4;

                std::cout << "\033[2;1H\033[1m FLUORESCENCE DECAY (CHANNEL " << dec.channel
                          << ") [" << (decay_log_scale ? "LOG10 SCALE" : "LINEAR SCALE") << "]\033[0m\n";

                double max_y = decay_log_scale ? log10((double)dec.max_count + 1.0) : (double)dec.max_count;
                if (max_y <= 0) max_y = 1.0;

                for (int r = 0; r < graph_h; r++) {
                    int row_y = 3 + r;
                    std::cout << "\033[" << row_y << ";1H";

                    double frac = (double)(graph_h - r) / graph_h;
                    double row_val = decay_log_scale ? pow(10.0, frac * max_y) - 1.0 : frac * max_y;
                    std::cout << std::setw(7) << (int)row_val << " │";

                    for (int col = 0; col < graph_w; col++) {
                        int bin_idx = (col * dec.n_bins) / graph_w;
                        if (bin_idx >= (int)dec.n_bins) bin_idx = dec.n_bins - 1;

                        uint32_t c = dec.counts[bin_idx];
                        double val = decay_log_scale ? log10((double)c + 1.0) : (double)c;
                        double norm = val / max_y * graph_h;
                        double fill = norm - (graph_h - 1 - r);

                        if (fill <= 0) std::cout << " ";
                        else if (fill >= 1.0) std::cout << "\033[32m█\033[0m";
                        else std::cout << "\033[32m▄\033[0m";
                    }
                }

                std::cout << "\033[" << (3 + graph_h) << ";1H        └";
                for (int i = 0; i < graph_w; i++) std::cout << "─";
                std::cout << "\n";
                std::cout << "\033[" << (4 + graph_h) << ";1H Channel: " << dec.channel
                          << " │ Bins: " << dec.n_bins << " │ Resolution: " << std::fixed << std::setprecision(3)
                          << (dec.bin_width_ns * 1000.0) << " ps/bin │ Peak Count: " << dec.max_count
                          << " │ Total Photons: " << dec.total_counts;
            }
        }
        /* TAB 4: OBJECTS */
        else if (current_tab == ViewTab::Objects) {
            int left_w = width / 3;
            int right_w = width - left_w - 1;

            for (int r = 0; r < main_h; r++) {
                std::cout << "\033[" << (r + 2) << ";1H";

                std::string left_text;
                if ((size_t)r < info->num_objects) {
                    const auto& obj = info->objects[r];
                    left_text = std::string(" ") + (obj.name[0] ? obj.name : obj.kind);
                }

                if ((size_t)r == selected_obj) {
                    std::cout << "\033[7m" << std::left << std::setw(left_w) << left_text.substr(0, left_w) << "\033[0m│";
                } else {
                    std::cout << std::left << std::setw(left_w) << left_text.substr(0, left_w) << "│";
                }

                std::string right_text;
                if (selected_obj < info->num_objects) {
                    const auto& obj = info->objects[selected_obj];
                    char sz[32];
                    pto_format_bytes(obj.size, sz, sizeof(sz));

                    if (r == 0) right_text = "UID:         0x" + (std::stringstream() << std::hex << obj.uid).str();
                    else if (r == 1) right_text = "Name:        " + std::string(obj.name[0] ? obj.name : "(none)");
                    else if (r == 2) right_text = "Kind:        " + std::string(obj.kind[0] ? obj.kind : "(none)");
                    else if (r == 3) right_text = "Encoding:    " + std::string(obj.encoding[0] ? obj.encoding : "(none)");
                    else if (r == 4) right_text = "Size:        " + std::string(sz) + " (" + std::to_string(obj.size) + " bytes)";
                    else if (r == 5) right_text = "Rows:        " + (obj.rows > 0 ? std::to_string(obj.rows) : std::string("-"));
                    else if (r == 6) right_text = "Alignment:   " + std::string(obj.aligned ? "8-byte aligned (OK)" : "unaligned");
                    else if (r == 7) right_text = "Payload At:  offset " + std::to_string(obj.offset);
                    else if (r == 9) right_text = "--- Object Metadata & Tags ---";
                    else if (r >= 10 && (size_t)(r - 10) < info->num_tags) {
                        const auto& tag = info->tags[r - 10];
                        if (tag.target == obj.uid || tag.target == 0) {
                            right_text = std::string("Tag ") + tag.name + " = " + tag.text;
                        }
                    }
                }
                std::cout << std::left << std::setw(right_w) << right_text.substr(0, right_w);
            }
        }
        /* TAB 5: EBML TREE */
        else if (current_tab == ViewTab::EBMLTree) {
            for (int r = 0; r < main_h; r++) {
                std::cout << "\033[" << (r + 2) << ";1H";
                std::string line;
                if ((size_t)r < info->num_elements) {
                    const auto& elem = info->elements[r];
                    char sz[32];
                    pto_format_bytes(elem.total_size, sz, sizeof(sz));
                    line = std::string(elem.depth * 2, ' ') + "├─ " + elem.name + " [0x" +
                           (std::stringstream() << std::hex << elem.id).str() + "] (" + sz + " at offset " +
                           std::to_string(elem.offset) + ")";
                }
                if ((int)line.length() < width) line.append(width - line.length(), ' ');
                std::cout << line.substr(0, width);
            }
        }

        /* Footer Hotkey Bar */
        std::cout << "\033[" << (height - 1) << ";1H\033[7m";
        if (current_tab == ViewTab::TimeTrace) {
            std::cout << " 1-5: Tabs │ ←/→: Pan │ +/-: Zoom │ [/]: Cursor │ Space: "
                      << (trace_playing ? "Pause " : "Play ") << "│ q: Quit ";
        } else if (current_tab == ViewTab::Decays) {
            std::cout << " 1-5: Tabs │ c: Channel (" << decay_channel_idx << ") │ l: "
                      << (decay_log_scale ? "Linear Scale " : "Log Scale ") << "│ q: Quit ";
        } else if (current_tab == ViewTab::Objects) {
            std::cout << " 1-5: Tabs │ ↑↓: Navigate │ e: Extract │ E: Extract All │ q: Quit ";
        } else {
            std::cout << " 1-5: Tabs │ Tab: Next View │ q: Quit ";
        }
        std::cout << "\033[0m";

        if (!message.empty()) {
            std::cout << "\033[" << height << ";1H\033[33m" << message.substr(0, width) << "\033[0m";
        }
        std::cout << std::flush;
    };

    auto on_key = [&](pto_tui::Key k) -> bool {
        if (k == pto_tui::Key::Num1) { current_tab = ViewTab::Summary; message = "Summary View"; }
        else if (k == pto_tui::Key::Num2) { current_tab = ViewTab::TimeTrace; message = "Dynamic Time Trace View"; }
        else if (k == pto_tui::Key::Num3) { current_tab = ViewTab::Decays; message = "Fluorescence Decay View"; }
        else if (k == pto_tui::Key::Num4) { current_tab = ViewTab::Objects; message = "Object List View"; }
        else if (k == pto_tui::Key::Num5) { current_tab = ViewTab::EBMLTree; message = "EBML Structural Tree View"; }
        else if (k == pto_tui::Key::Tab) {
            int t = (int)current_tab + 1;
            if (t > 4) t = 0;
            current_tab = (ViewTab)t;
        }
        else if (k == pto_tui::Key::Space) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_playing = !trace_playing;
                message = trace_playing ? "Trace animation started" : "Trace animation paused";
            }
        }
        else if (k == pto_tui::Key::LogScale) {
            if (current_tab == ViewTab::Decays) {
                decay_log_scale = !decay_log_scale;
                message = decay_log_scale ? "Switched to Log10 scale" : "Switched to Linear scale";
            }
        }
        else if (k == pto_tui::Key::Channel) {
            if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx + 1) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            }
        }
        else if (k == pto_tui::Key::Left) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_offset -= 5;
                message = "Pan left";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx - 1 + insp.num_decays) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else {
                int t = (int)current_tab - 1;
                if (t < 0) t = 4;
                current_tab = (ViewTab)t;
                message = "Switched tab";
            }
        }
        else if (k == pto_tui::Key::CtrlLeft) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_offset -= 50;
                message = "Fast pan left (-50)";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx - 1 + insp.num_decays) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else {
                int t = (int)current_tab - 1;
                if (t < 0) t = 4;
                current_tab = (ViewTab)t;
                message = "Switched tab";
            }
        }
        else if (k == pto_tui::Key::Right) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_offset += 5;
                message = "Pan right";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx + 1) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else {
                int t = (int)current_tab + 1;
                if (t > 4) t = 0;
                current_tab = (ViewTab)t;
                message = "Switched tab";
            }
        }
        else if (k == pto_tui::Key::CtrlRight) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_offset += 50;
                message = "Fast pan right (+50)";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx + 1) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else {
                int t = (int)current_tab + 1;
                if (t > 4) t = 0;
                current_tab = (ViewTab)t;
                message = "Switched tab";
            }
        }
        else if (k == pto_tui::Key::Up) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_zoom *= 2;
                if (trace_zoom > 16) trace_zoom = 16;
                message = "Zoom in (" + std::to_string(trace_zoom) + "x)";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx - 1 + insp.num_decays) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else if (current_tab == ViewTab::Objects && selected_obj > 0) {
                selected_obj--;
            }
        }
        else if (k == pto_tui::Key::CtrlUp) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_zoom = 16;
                message = "Zoom max (16x)";
            } else if (current_tab == ViewTab::Objects) {
                if (selected_obj >= 5) selected_obj -= 5;
                else selected_obj = 0;
            }
        }
        else if (k == pto_tui::Key::Down) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_zoom /= 2;
                if (trace_zoom < 1) trace_zoom = 1;
                message = "Zoom out (" + std::to_string(trace_zoom) + "x)";
            } else if (current_tab == ViewTab::Decays && insp.num_decays > 0) {
                decay_channel_idx = (decay_channel_idx + 1) % insp.num_decays;
                message = "Selected Decay Channel " + std::to_string(insp.decays[decay_channel_idx].channel);
            } else if (current_tab == ViewTab::Objects && selected_obj + 1 < info->num_objects) {
                selected_obj++;
            }
        }
        else if (k == pto_tui::Key::CtrlDown) {
            if (current_tab == ViewTab::TimeTrace) {
                trace_zoom = 1;
                message = "Zoom reset (1x)";
            } else if (current_tab == ViewTab::Objects) {
                selected_obj += 5;
                if (selected_obj >= info->num_objects) selected_obj = info->num_objects > 0 ? info->num_objects - 1 : 0;
            }
        }
        else if (k == pto_tui::Key::Extract) {
            if (selected_obj < info->num_objects) {
                const auto& obj = info->objects[selected_obj];
                const char* name = obj.name[0] ? obj.name : "extracted.bin";
                if (pto_read_extract(info->filename, &obj, name) == 0) {
                    message = std::string("Extracted ") + name;
                } else {
                    message = std::string("Failed to extract ") + name;
                }
            }
        }
        else if (k == pto_tui::Key::ExtractAll) {
            size_t count = 0;
            for (size_t i = 0; i < info->num_objects; i++) {
                const auto& obj = info->objects[i];
                char outname[512];
                snprintf(outname, sizeof(outname), "extracted_%" PRIu64 "_%s", obj.uid, obj.name[0] ? obj.name : "object.bin");
                if (pto_read_extract(info->filename, &obj, outname) == 0) count++;
            }
            message = "Extracted " + std::to_string(count) + " objects";
        }
        return false;
    };

    auto needs_redraw = [&]() -> bool {
        return (current_tab == ViewTab::TimeTrace && trace_playing);
    };

    app.run(draw, on_key, needs_redraw);

    if (info == &local_info) {
        pto_read_close(&local_info);
    }
    return 0;
}

#ifndef PTO_HAS_TUI
int main(int argc, char** argv) {
    return ptoview_main(argc, argv, NULL);
}
#endif

// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;
import io.github.fluorescencetools.tttrlib.imagej.core.DetectorSetup;

import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.prefs.PrefService;
import org.scijava.ui.UIService;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.DefaultCellEditor;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JDialog;
import javax.swing.JFileChooser;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.event.TableModelEvent;
import javax.swing.event.TableModelListener;
import javax.swing.filechooser.FileNameExtensionFilter;
import javax.swing.table.DefaultTableModel;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridLayout;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Editor for a chisurf detector setup: PIE windows and detectors, over the decay
 * they refer to.
 *
 * <p>Follows chisurf's <em>Setup:Channel Definition</em> page — setup selector,
 * TTTR reading routine, a PIE-window table and a detector table — and, as there,
 * draws the micro-time decay with every window and detector gate shaded on it.
 * Those bounds are raw micro-time channels, so without the curve the numbers are
 * guesswork.</p>
 *
 * <p>{@link DefineDetectors} edits the same file one field at a time and remains
 * available for macros and headless use.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Detector Definition...")
public class DetectorSetupEditor implements Command {

    static { NativeLoader.load(); }

    @Parameter
    private LogService log;

    @Parameter
    private PrefService prefs;

    @Parameter(required = false)
    private UIService ui;

    @Override
    public void run() {
        if (ui != null && ui.isHeadless()) {
            log.error("tttrlib: the setup editor needs a display; "
                    + "use 'Detector Definition (headless)' in scripts.");
            return;
        }
        try {
            SwingUtilities.invokeAndWait(() -> new EditorDialog().setVisible(true));
        } catch (Exception e) {
            log.error("tttrlib: could not open the setup editor: " + e);
        }
    }

    // ── dialog ─────────────────────────────────────────────────────────────

    private final class EditorDialog extends JDialog {

        private final DefaultTableModel windowModel =
                new DefaultTableModel(new Object[] { "Window", "Start", "End" }, 0);
        private final DefaultTableModel detectorModel = new DefaultTableModel(
                new Object[] { "Detector", "Channels", "Micro-time ranges",
                               "G-factor", "l1", "l2" }, 0);

        private final JComboBox<String> setupCombo = new JComboBox<>();
        private final JCheckBox polarizationResolved =
                new JCheckBox("Polarization resolved", true);
        private final JTextField setupName = new JTextField(12);
        private final JLabel fileLabel = new JLabel("(unsaved)");

        private final JTextField fileTypeField = new JTextField(10);
        private final JTextField macroResField = new JTextField(8);
        private final JTextField microResField = new JTextField(8);
        private final JLabel channelsLabel = new JLabel("—");
        private final JLabel photonsLabel = new JLabel("—");

        private final DecayPlotPanel plot = new DecayPlotPanel();

        private File file;
        private DetectorSetup setup;
        private boolean loading;

        EditorDialog() {
            super((java.awt.Frame) null, "tttrlib — Detector Definition", true);
            setLayout(new BorderLayout(6, 6));
            ((JPanel) getContentPane()).setBorder(BorderFactory.createEmptyBorder(8, 8, 8, 8));

            add(north(), BorderLayout.NORTH);
            add(centre(), BorderLayout.CENTER);
            add(buttons(), BorderLayout.SOUTH);

            // Redraw the overlays as the tables are edited: that feedback is the
            // reason the plot is here at all.
            final TableModelListener redraw = e -> { if (!loading) refreshOverlays(); };
            windowModel.addTableModelListener(redraw);
            detectorModel.addTableModelListener(redraw);

            final String last = prefs.get(TttrlibSettings.class,
                    TttrlibSettings.KEY_SETUP_FILE, "");
            if (last != null && !last.isEmpty() && new File(last).exists()) {
                load(new File(last), null);
            } else {
                setup = DetectorSetup.empty("default");
                refresh();
            }
            setSize(1000, 780);
            setLocationRelativeTo(null);
        }

        // ── layout ────────────────────────────────────────────────────────

        private JPanel north() {
            final JPanel p = new JPanel();
            p.setLayout(new BoxLayout(p, BoxLayout.Y_AXIS));

            final JPanel row = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 3));
            row.add(new JLabel("Setup:"));
            setupCombo.addActionListener(e -> {
                if (loading || setupCombo.getSelectedItem() == null) return;
                if (file != null) load(file, (String) setupCombo.getSelectedItem());
            });
            row.add(setupCombo);
            row.add(new JLabel("Name:"));
            row.add(setupName);
            row.add(polarizationResolved);
            row.add(Box.createHorizontalStrut(10));
            row.add(new JLabel("File:"));
            fileLabel.setFont(fileLabel.getFont().deriveFont(Font.PLAIN, 11f));
            row.add(fileLabel);
            p.add(row);

            p.add(readingRoutine());
            return p;
        }

        /** Read-only view of what the TTTR file says, as in chisurf's box. */
        private JPanel readingRoutine() {
            final JPanel box = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 3));
            box.setBorder(BorderFactory.createTitledBorder("TTTR reading routine"));
            final JButton read = new JButton("Read TTTR file…");
            read.addActionListener(e -> readTttr());
            box.add(read);
            box.add(new JLabel("Type:"));
            fileTypeField.setEditable(false);
            box.add(fileTypeField);
            box.add(new JLabel("Macro (ns):"));
            macroResField.setEditable(false);
            box.add(macroResField);
            box.add(new JLabel("Micro (ns):"));
            microResField.setEditable(false);
            box.add(microResField);
            box.add(new JLabel("Channels:"));
            box.add(channelsLabel);
            box.add(new JLabel("Photons:"));
            box.add(photonsLabel);
            return box;
        }

        private JPanel centre() {
            final JPanel p = new JPanel();
            p.setLayout(new BoxLayout(p, BoxLayout.Y_AXIS));

            final JPanel plotBox = new JPanel(new BorderLayout());
            plotBox.setBorder(BorderFactory.createTitledBorder(
                    "Micro-time decay — windows solid, detector gates dashed"));
            plotBox.add(plot, BorderLayout.CENTER);
            p.add(plotBox);

            p.add(tableBox("PIE windows (raw micro-time channels)", windowModel, 3, true));
            p.add(tableBox("Detectors — channels e.g. 0,1 · ranges e.g. 0,2048;2048,4095",
                    detectorModel, 5, false));

            final JTextArea help = new JTextArea(
                    "Bounds are RAW micro-time channels of the file, never nanoseconds and "
                    + "never divided by any binning — the same convention chisurf uses.\n"
                    + "A detector with no ranges is ungated. Each window x detector pair is "
                    + "gated by the intersection of the two; a pair that does not overlap is "
                    + "dropped rather than left ungated.");
            help.setEditable(false);
            help.setOpaque(false);
            help.setFont(help.getFont().deriveFont(Font.PLAIN, 11f));
            help.setBorder(BorderFactory.createEmptyBorder(2, 4, 2, 4));
            p.add(help);
            return p;
        }

        private JPanel tableBox(String title, DefaultTableModel model, int rows,
                                boolean isWindows) {
            final JTable table = new JTable(model);
            table.setRowHeight(21);
            table.putClientProperty("terminateEditOnFocusLost", Boolean.TRUE);
            for (int c = 0; c < model.getColumnCount(); c++) {
                table.getColumnModel().getColumn(c)
                        .setCellEditor(new DefaultCellEditor(new JTextField()));
            }
            final JScrollPane sc = new JScrollPane(table);
            sc.setPreferredSize(new Dimension(960, 21 * rows + 26));

            final JPanel bar = new JPanel(new FlowLayout(FlowLayout.LEFT, 4, 2));
            final JButton add = new JButton("Add");
            add.addActionListener(e -> {
                if (isWindows) {
                    model.addRow(new Object[] { "window" + (model.getRowCount() + 1),
                                                "0", "2048" });
                } else {
                    model.addRow(new Object[] { "detector" + (model.getRowCount() + 1),
                                                "0", "", "1.0", "0.0", "0.0" });
                }
            });
            final JButton del = new JButton("Remove selected");
            del.addActionListener(e -> {
                final int[] r = table.getSelectedRows();
                for (int i = r.length - 1; i >= 0; i--) model.removeRow(r[i]);
            });
            bar.add(add);
            bar.add(del);

            final JPanel p = new JPanel(new BorderLayout(4, 2));
            p.setBorder(BorderFactory.createTitledBorder(title));
            p.add(sc, BorderLayout.CENTER);
            p.add(bar, BorderLayout.SOUTH);
            return p;
        }

        private JPanel buttons() {
            final JPanel p = new JPanel(new FlowLayout(FlowLayout.RIGHT, 6, 4));
            final JButton open = new JButton("Open…");
            open.addActionListener(e -> {
                final File f = choose(false);
                if (f != null) load(f, null);
            });
            final JButton json = new JButton("Edit JSON…");
            json.addActionListener(e -> editJson());
            final JButton save = new JButton("Save");
            save.addActionListener(e -> save(false));
            final JButton saveAs = new JButton("Save as…");
            saveAs.addActionListener(e -> save(true));
            final JButton close = new JButton("Close");
            close.addActionListener(e -> dispose());
            p.add(open);
            p.add(json);
            p.add(save);
            p.add(saveAs);
            p.add(close);
            return p;
        }

        // ── behaviour ─────────────────────────────────────────────────────

        private File choose(boolean forSave) {
            final JFileChooser fc = new JFileChooser();
            fc.setFileFilter(new FileNameExtensionFilter("Detector setup (*.json)", "json"));
            if (file != null) fc.setSelectedFile(file);
            final int r = forSave ? fc.showSaveDialog(this) : fc.showOpenDialog(this);
            return r == JFileChooser.APPROVE_OPTION ? fc.getSelectedFile() : null;
        }

        private void load(File f, String which) {
            try {
                setup = DetectorSetup.load(f.getAbsolutePath(), which);
                file = f;
                refresh();
            } catch (Exception e) {
                JOptionPane.showMessageDialog(this, "Could not read the setup:\n" + e,
                        "tttrlib", JOptionPane.ERROR_MESSAGE);
            }
        }

        private void refresh() {
            loading = true;
            try {
                windowModel.setRowCount(0);
                detectorModel.setRowCount(0);
                setupName.setText(setup.setupName());
                polarizationResolved.setSelected(setup.polarizationResolved());
                fileLabel.setText(file == null ? "(unsaved)" : file.getAbsolutePath());

                setupCombo.removeAllItems();
                for (String n : setup.setupNames()) setupCombo.addItem(n);
                setupCombo.setSelectedItem(setup.setupName());

                for (Map.Entry<String, int[]> w : setup.windows().entrySet()) {
                    windowModel.addRow(new Object[] { w.getKey(),
                            String.valueOf(w.getValue()[0]),
                            String.valueOf(w.getValue()[1]) });
                }
                for (Map.Entry<String, DetectorSetup.Detector> d : setup.detectors().entrySet()) {
                    final DetectorSetup.Detector det = d.getValue();
                    detectorModel.addRow(new Object[] { d.getKey(),
                            DefineDetectors.join(det.channels),
                            DefineDetectors.joinRanges(det.microTimeRanges),
                            String.valueOf(det.gFactor), String.valueOf(det.l1),
                            String.valueOf(det.l2) });
                }
                final double res = setup.microTimeResolutionNs();
                if (res > 0) microResField.setText(String.format("%.6f", res));
            } finally {
                loading = false;
            }
            refreshOverlays();
        }

        /** Shade every window and detector gate currently in the tables. */
        private void refreshOverlays() {
            final List<DecayPlotPanel.Span> spans = new ArrayList<>();
            for (int r = 0; r < windowModel.getRowCount(); r++) {
                final Integer s = intOrNull(windowModel.getValueAt(r, 1));
                final Integer e = intOrNull(windowModel.getValueAt(r, 2));
                if (s == null || e == null || e <= s) continue;
                spans.add(new DecayPlotPanel.Span(str(windowModel.getValueAt(r, 0)),
                        s, e, DecayPlotPanel.paletteColor(r), false));
            }
            for (int r = 0; r < detectorModel.getRowCount(); r++) {
                final String name = str(detectorModel.getValueAt(r, 0));
                for (int[] range
                        : DefineDetectors.parseRanges(str(detectorModel.getValueAt(r, 2)))) {
                    if (range[1] <= range[0]) continue;
                    spans.add(new DecayPlotPanel.Span(name, range[0], range[1],
                            DecayPlotPanel.paletteColor(r + 3), true));
                }
            }
            plot.setSpans(spans);
        }

        /** Load a TTTR file: its decay drives the plot, its header the routine box. */
        private void readTttr() {
            final JFileChooser fc = new JFileChooser();
            fc.setFileFilter(new FileNameExtensionFilter(
                    "TTTR (*.ptu, *.ht3, *.spc)", "ptu", "ht3", "spc"));
            if (fc.showOpenDialog(this) != JFileChooser.APPROVE_OPTION) return;
            try {
                final TTTR t = new TTTR(fc.getSelectedFile().getAbsolutePath());
                final int nBins = (int) t.get_number_of_micro_time_channels();
                final double[] hist = new double[Math.max(nBins, 1)];
                final int n = t.get_microtime_histogram_into(hist, new VectorInt32(), 1);
                final double res = t.get_micro_time_resolution_s() * 1e9;
                plot.setDecay(n < hist.length
                        ? java.util.Arrays.copyOf(hist, Math.max(n, 0)) : hist, res);

                final int nc = t.get_used_routing_channels_into(new int[0]);
                final int[] used = new int[Math.max(nc, 0)];
                if (nc > 0) t.get_used_routing_channels_into(used);
                java.util.Arrays.sort(used);

                fileTypeField.setText(fc.getSelectedFile().getName());
                microResField.setText(String.format("%.6f", res));
                macroResField.setText(String.format("%.6f",
                        t.get_header().getMacro_time_resolution()));
                channelsLabel.setText(DefineDetectors.join(used));
                photonsLabel.setText(String.valueOf(t.size()));
                refreshOverlays();
            } catch (Throwable e) {
                JOptionPane.showMessageDialog(this, "Could not read the TTTR file:\n" + e,
                        "tttrlib", JOptionPane.ERROR_MESSAGE);
            }
        }

        /** Raw JSON view, as chisurf's "Edit JSON" button offers. */
        private void editJson() {
            if (file == null || !file.exists()) {
                JOptionPane.showMessageDialog(this, "Save the setup first.",
                        "tttrlib", JOptionPane.INFORMATION_MESSAGE);
                return;
            }
            try {
                final String text = new String(java.nio.file.Files.readAllBytes(file.toPath()),
                        java.nio.charset.StandardCharsets.UTF_8);
                final JTextArea area = new JTextArea(text, 30, 80);
                area.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 11));
                final int r = JOptionPane.showConfirmDialog(this, new JScrollPane(area),
                        "tttrlib — " + file.getName(), JOptionPane.OK_CANCEL_OPTION,
                        JOptionPane.PLAIN_MESSAGE);
                if (r == JOptionPane.OK_OPTION) {
                    java.nio.file.Files.write(file.toPath(),
                            area.getText().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    load(file, null);
                }
            } catch (Exception e) {
                JOptionPane.showMessageDialog(this, "JSON edit failed:\n" + e,
                        "tttrlib", JOptionPane.ERROR_MESSAGE);
            }
        }

        private void save(boolean askForPath) {
            final List<String> errors = new ArrayList<>();
            final List<Object[]> windows = new ArrayList<>();
            for (int r = 0; r < windowModel.getRowCount(); r++) {
                final String name = str(windowModel.getValueAt(r, 0));
                if (name.isEmpty()) { errors.add("window row " + (r + 1) + ": no name"); continue; }
                final Integer s = intOrNull(windowModel.getValueAt(r, 1));
                final Integer e = intOrNull(windowModel.getValueAt(r, 2));
                if (s == null || e == null) {
                    errors.add("window '" + name + "': start and end must be whole numbers");
                } else if (e <= s) {
                    // A zero-width window reads as "no gate" downstream and would
                    // silently admit every photon.
                    errors.add("window '" + name + "': end must be greater than start");
                } else {
                    windows.add(new Object[] { name, s, e });
                }
            }

            final List<DetectorSetup.Detector> detectors = new ArrayList<>();
            for (int r = 0; r < detectorModel.getRowCount(); r++) {
                final String name = str(detectorModel.getValueAt(r, 0));
                if (name.isEmpty()) { errors.add("detector row " + (r + 1) + ": no name"); continue; }
                final DetectorSetup.Detector d = new DetectorSetup.Detector();
                d.name = name;
                d.channels = DefineDetectors.parseInts(str(detectorModel.getValueAt(r, 1)));
                if (d.channels.length == 0) {
                    errors.add("detector '" + name + "': needs at least one channel");
                    continue;
                }
                d.microTimeRanges = DefineDetectors.parseRanges(str(detectorModel.getValueAt(r, 2)));
                final Double g = dblOrNull(detectorModel.getValueAt(r, 3));
                final Double l1 = dblOrNull(detectorModel.getValueAt(r, 4));
                final Double l2 = dblOrNull(detectorModel.getValueAt(r, 5));
                if (g == null || l1 == null || l2 == null) {
                    errors.add("detector '" + name + "': G-factor, l1 and l2 must be numbers");
                    continue;
                }
                d.gFactor = g;
                d.l1 = l1;
                d.l2 = l2;
                detectors.add(d);
            }
            if (detectors.isEmpty()) errors.add("at least one detector is required");
            if (!errors.isEmpty()) {
                JOptionPane.showMessageDialog(this,
                        "Not saved:\n  • " + String.join("\n  • ", errors),
                        "tttrlib", JOptionPane.WARNING_MESSAGE);
                return;
            }

            File target = file;
            if (askForPath || target == null) {
                target = choose(true);
                if (target == null) return;
                if (!target.getName().toLowerCase().endsWith(".json")) {
                    target = new File(target.getAbsolutePath() + ".json");
                }
            }
            try {
                final String name = str(setupName.getText()).isEmpty()
                        ? "default" : str(setupName.getText());
                final DetectorSetup out = target.exists()
                        ? DetectorSetup.load(target.getAbsolutePath(), name)
                        : DetectorSetup.empty(name);
                out.setPolarizationResolved(polarizationResolved.isSelected());
                for (Object[] w : windows) {
                    out.putWindow((String) w[0], (Integer) w[1], (Integer) w[2]);
                }
                for (DetectorSetup.Detector d : detectors) out.putDetector(d);
                out.save(target.getAbsolutePath());

                file = target;
                setup = DetectorSetup.load(target.getAbsolutePath(), name);
                refresh();
                prefs.put(TttrlibSettings.class, TttrlibSettings.KEY_SETUP_FILE,
                        target.getAbsolutePath());
                log.info("tttrlib: setup '" + name + "' saved to " + target);
            } catch (Exception e) {
                JOptionPane.showMessageDialog(this, "Could not save:\n" + e,
                        "tttrlib", JOptionPane.ERROR_MESSAGE);
            }
        }
    }

    // Hoisted out of EditorDialog: Java 8 does not allow static members in an
    // inner (non-static) class.
    static String str(Object o) { return o == null ? "" : o.toString().trim(); }

    static Integer intOrNull(Object o) {
        try { return Integer.valueOf(str(o)); } catch (Exception e) { return null; }
    }

    static Double dblOrNull(Object o) {
        final String s = str(o);
        if (s.isEmpty()) return null;
        try { return Double.valueOf(s); } catch (Exception e) { return null; }
    }
}

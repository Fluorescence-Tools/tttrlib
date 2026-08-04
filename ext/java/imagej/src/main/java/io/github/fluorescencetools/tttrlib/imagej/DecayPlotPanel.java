// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import javax.swing.JPanel;
import javax.swing.ToolTipManager;

import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.List;

/**
 * Micro-time decay with PIE-window and detector-gate overlays.
 *
 * <p>The point of the editor: window and gate bounds are raw micro-time channels,
 * and picking them without seeing where the decay actually rises and falls is
 * guesswork. Shading each range over the curve makes the numbers in the tables
 * checkable at a glance.</p>
 *
 * <p>Counts are drawn on a log scale — a fluorescence decay spans orders of
 * magnitude, and on a linear axis the tail (where the gates usually sit) is a
 * flat line on the axis.</p>
 */
class DecayPlotPanel extends JPanel {

    /** A shaded span: {@code [start, end)} in micro-time channels. */
    static final class Span {
        final String label;
        final int start, end;
        final Color color;
        final boolean detector;

        Span(String label, int start, int end, Color color, boolean detector) {
            this.label = label;
            this.start = start;
            this.end = end;
            this.color = color;
            this.detector = detector;
        }
    }

    /** Distinguishable and colour-blind-safe enough for a handful of overlays. */
    private static final Color[] PALETTE = {
        new Color(0x1f77b4), new Color(0xd62728), new Color(0x2ca02c),
        new Color(0x9467bd), new Color(0xff7f0e), new Color(0x17becf),
    };

    private double[] decay = new double[0];
    private double resolutionNs = -1;
    private final List<Span> spans = new ArrayList<>();
    private int cursorChannel = -1;

    DecayPlotPanel() {
        setPreferredSize(new Dimension(900, 190));
        setBackground(Color.WHITE);
        ToolTipManager.sharedInstance().registerComponent(this);
        addMouseMotionListener(new MouseAdapter() {
            @Override
            public void mouseMoved(MouseEvent e) {
                cursorChannel = channelAt(e.getX());
                repaint();
            }
        });
    }

    static Color paletteColor(int i) { return PALETTE[Math.floorMod(i, PALETTE.length)]; }

    void setDecay(double[] d, double resolutionNs) {
        this.decay = d == null ? new double[0] : d;
        this.resolutionNs = resolutionNs;
        repaint();
    }

    void setSpans(List<Span> s) {
        spans.clear();
        if (s != null) spans.addAll(s);
        repaint();
    }

    boolean hasDecay() { return decay.length > 0; }

    private static final int PAD_L = 52, PAD_R = 10, PAD_T = 10, PAD_B = 28;

    private int channelAt(int px) {
        if (decay.length == 0) return -1;
        final int w = getWidth() - PAD_L - PAD_R;
        if (w <= 0 || px < PAD_L || px > PAD_L + w) return -1;
        return (int) ((px - PAD_L) * (long) decay.length / w);
    }

    @Override
    public String getToolTipText(MouseEvent e) {
        final int ch = channelAt(e.getX());
        if (ch < 0 || ch >= decay.length) return null;
        final StringBuilder b = new StringBuilder("<html>channel ").append(ch);
        if (resolutionNs > 0) {
            b.append(" &nbsp;(").append(String.format("%.3f ns", ch * resolutionNs)).append(')');
        }
        b.append("<br>counts ").append((long) decay[ch]);
        for (Span s : spans) {
            if (ch >= s.start && ch < s.end) b.append("<br>in <b>").append(s.label).append("</b>");
        }
        return b.append("</html>").toString();
    }

    @Override
    protected void paintComponent(Graphics g0) {
        super.paintComponent(g0);
        final Graphics2D g = (Graphics2D) g0;
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        final int w = getWidth() - PAD_L - PAD_R;
        final int h = getHeight() - PAD_T - PAD_B;
        if (w <= 0 || h <= 0) return;

        g.setColor(new Color(0xf7f7f7));
        g.fillRect(PAD_L, PAD_T, w, h);

        if (decay.length == 0) {
            g.setColor(Color.GRAY);
            g.setFont(getFont().deriveFont(Font.ITALIC));
            g.drawString("Load a TTTR file to see its decay — the window and gate "
                    + "bounds below are micro-time channels of that decay.",
                    PAD_L + 12, PAD_T + h / 2);
            drawFrame(g, w, h);
            return;
        }

        double max = 1;
        for (double v : decay) if (v > max) max = v;
        final double logMax = Math.log10(max + 1);

        // Range overlays first, so the curve stays readable on top of them.
        int i = 0;
        for (Span s : spans) {
            final int x0 = PAD_L + (int) ((long) s.start * w / decay.length);
            final int x1 = PAD_L + (int) ((long) Math.min(s.end, decay.length) * w / decay.length);
            final Color c = s.color;
            g.setColor(new Color(c.getRed(), c.getGreen(), c.getBlue(), s.detector ? 26 : 46));
            g.fillRect(x0, PAD_T, Math.max(x1 - x0, 1), h);
            g.setColor(new Color(c.getRed(), c.getGreen(), c.getBlue(), 190));
            g.setStroke(new BasicStroke(s.detector ? 1f : 1.6f, BasicStroke.CAP_BUTT,
                    BasicStroke.JOIN_MITER, 10f,
                    s.detector ? new float[] { 3f, 3f } : null, 0f));
            g.drawLine(x0, PAD_T, x0, PAD_T + h);
            g.drawLine(x1, PAD_T, x1, PAD_T + h);
            g.setFont(getFont().deriveFont(Font.BOLD, 10f));
            g.drawString(s.label, x0 + 3, PAD_T + 11 + 12 * (i % 3));
            i++;
        }
        g.setStroke(new BasicStroke(1f));

        // Decay, log counts.
        g.setColor(new Color(0x333333));
        int prevX = -1, prevY = -1;
        for (int c = 0; c < decay.length; c++) {
            final int x = PAD_L + (int) ((long) c * w / decay.length);
            final int y = PAD_T + h
                    - (int) (Math.log10(decay[c] + 1) / logMax * h);
            if (prevX >= 0) g.drawLine(prevX, prevY, x, y);
            prevX = x;
            prevY = y;
        }

        if (cursorChannel >= 0 && cursorChannel < decay.length) {
            final int x = PAD_L + (int) ((long) cursorChannel * w / decay.length);
            g.setColor(new Color(0, 0, 0, 90));
            g.drawLine(x, PAD_T, x, PAD_T + h);
        }

        drawFrame(g, w, h);

        g.setColor(Color.DARK_GRAY);
        g.setFont(getFont().deriveFont(9f));
        g.drawString("counts (log)", 4, PAD_T + 10);
        g.drawString("0", PAD_L, PAD_T + h + 14);
        final String right = resolutionNs > 0
                ? String.format("%d ch  (%.1f ns)", decay.length,
                                decay.length * resolutionNs)
                : decay.length + " ch";
        g.drawString(right, PAD_L + w - g.getFontMetrics().stringWidth(right),
                PAD_T + h + 14);
        g.drawString(String.format("max %d", (long) max), PAD_L + 4, PAD_T + h + 14);
    }

    private void drawFrame(Graphics2D g, int w, int h) {
        g.setColor(new Color(0xbbbbbb));
        g.drawRect(PAD_L, PAD_T, w, h);
    }
}

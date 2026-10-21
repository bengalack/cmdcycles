#!/usr/bin/env python3
"""Generates SVG charts from cmdcycle reports (markdown files).

Scans --data-dir for .md files. Cycle reports ("## CYCLES") give, per mode and
per test, two charts:
  * shape:     cycles against width, one line per height (most common value of
               all models per cell)
  * deviation: per size, each model's value minus that most common value, one
               colour per model
Histogram reports ("## HISTOGRAM") give, per size, mode and test, one chart:
  * % of attempts against cycles, one line per model

With --png the charts are written as PNG instead: each SVG is converted with
cairosvg if installed, else with a headless Chrome or Edge, and then removed.

Usage: python gen_charts.py [options]
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
from collections import Counter, defaultdict
from xml.sax.saxutils import escape

TEST_ORDER = ["LMMM", "HMMM", "YMMM", "HMMV", "LMMV", "LINE"]
MODE_ORDER = ["Active display", "No sprites", "No display"]

# Colour-blind friendly palette (Okabe-Ito), then some extras
PALETTE = ["#0072B2", "#E69F00", "#009E73", "#D55E00", "#CC79A7", "#56B4E9",
           "#F0E442", "#000000", "#999999", "#8C564B"]


# ---------------------------------------------------------------------------
# Parsing

def parse_report(path):
    """Returns a dict with model, mode and either 'cycles' or 'hist', or None."""
    with open(path, encoding="latin-1") as f:
        lines = [l.rstrip("\r\n") for l in f]
    if not lines or not lines[0].startswith("cmdcycle"):
        return None

    model = lines[0].rsplit(" - ", 1)[-1].strip()
    model = re.sub(r"\(.*?\)", "", model).strip().replace("_", " ")

    mode = None
    for l in lines[:10]:
        m = re.search(r"mode: ([^.(]+)\.", l)
        if m:
            mode = m.group(1).strip()
            break

    rep = {"path": path, "model": model, "mode": mode}

    if any(l.startswith("## CYCLES") for l in lines):
        rep["cycles"] = parse_cycles(lines)
    elif any(l.startswith("## HISTOGRAM") for l in lines):
        rep["size"], rep["hist"] = parse_hist(lines)
    else:
        return None
    return rep


def test_key(name):
    for t in TEST_ORDER:
        if t in name:
            return t
    return name


def parse_cycles(lines):
    """{test name: 8x8 list [height][width]} from the first CYCLES section."""
    tables = {}
    name = None
    rows = []
    in_cycles = False
    for l in lines:
        if l.startswith("## "):
            if in_cycles:
                break                       # e.g. the -v diagnostic tables
            in_cycles = l.startswith("## CYCLES")
            continue
        if not in_cycles:
            continue
        m = re.match(r"\|\s*((?:Copy|Fill|Line):[^|]*?)\s*\|", l)
        if m:
            name, rows = m.group(1), []
            continue
        m = re.match(r"\|\s*([1-8])\s*\|(.*)\|\s*$", l)
        if m and name:
            rows.append([int(x) for x in m.group(2).split("|")])
            if len(rows) == 8:
                tables[name] = rows
                name = None
    return tables


def parse_hist(lines):
    """(size string, {test name: (minimum, [counts])}) from the Count table."""
    size = None
    hist = {}
    for l in lines:
        m = re.match(r"### Size (\d+)x(\d+)", l)
        if m:
            size = (int(m.group(1)), int(m.group(2)))
        if "Normalized" in l:
            break                           # only the Count table
        m = re.match(r"\|\s*((?:Copy|Fill|Line):[^|]*?)\s*\|\s*(\d+)\s*\|\s*([\d ]+)\s*$", l)
        if m:
            hist[m.group(1)] = (int(m.group(2)), [int(x) for x in m.group(3).split()])
    return size, hist


# ---------------------------------------------------------------------------
# SVG helpers

def nice_ticks(lo, hi, n=5):
    """Round tick values covering [lo, hi]."""
    if hi <= lo:
        hi = lo + 1
    raw = (hi - lo) / n
    mag = 10 ** len(str(int(raw))) / 10 if raw >= 1 else 1
    for m in (1, 2, 2.5, 5, 10):
        step = m * mag
        if (hi - lo) / step <= n:
            break
    start = int(lo // step) * step
    ticks = []
    v = start
    while v <= hi + step * 0.001:
        ticks.append(v)
        v += step
    if ticks[-1] < hi:
        ticks.append(ticks[-1] + step)
    return ticks


def fmt(v):
    return str(int(v)) if float(v).is_integer() else ("%g" % v)


class Svg:
    def __init__(self, w, h, bg, fg):
        self.w, self.h, self.fg = w, h, fg
        self.parts = [
            '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
            'font-family="sans-serif">' % (w, h, w, h),
            '<rect x="0" y="0" width="%d" height="%d" fill="%s"/>' % (w, h, bg),
        ]

    def text(self, x, y, s, size=11, anchor="start", weight="normal", fill=None, rotate=None):
        tr = ' transform="rotate(%d %.1f %.1f)"' % (rotate, x, y) if rotate else ""
        self.parts.append('<text x="%.1f" y="%.1f" font-size="%d" text-anchor="%s" font-weight="%s" fill="%s"%s>%s</text>'
                          % (x, y, size, anchor, weight, fill or self.fg, tr, escape(s)))

    def line(self, x1, y1, x2, y2, stroke=None, width=1, opacity=1.0, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%g" stroke-opacity="%g"%s/>'
                          % (x1, y1, x2, y2, stroke or self.fg, width, opacity, d))

    def polyline(self, pts, stroke, width=2, opacity=1.0):
        p = " ".join("%.1f,%.1f" % xy for xy in pts)
        self.parts.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%g" stroke-opacity="%g" stroke-linejoin="round"/>'
                          % (p, stroke, width, opacity))

    def circle(self, x, y, r, fill):
        self.parts.append('<circle cx="%.1f" cy="%.1f" r="%g" fill="%s"/>' % (x, y, r, fill))

    def save(self, path):
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(self.parts + ["</svg>\n"]))


class Frame:
    """Titles, plot area, legend and axes of one chart."""

    def __init__(self, args, title, subtitle, mode, legend):
        self.svg = Svg(args.width, args.height, args.background_color, args.text_color)
        w, h = args.width, args.height
        s = self.svg
        s.text(w / 2, 24, title, 17, "middle", "bold")
        s.text(w / 2, 43, subtitle, 13, "middle")
        s.text(w / 2, 59, "Mode: " + mode, 11, "middle")

        # Legend: up to two columns at the bottom
        self.legend = legend
        cols = 1 if len(legend) <= 3 else 2
        rows = (len(legend) + cols - 1) // cols
        legend_h = rows * 15 + 6 if legend else 0
        self.left, self.right = 58, w - 16
        self.top, self.bottom = 74, h - 46 - legend_h
        col_w = (w - 32) / cols
        for i, (label, colour) in enumerate(legend):
            x = 16 + (i % cols) * col_w
            y = self.bottom + 52 + (i // cols) * 15
            s.line(x, y - 4, x + 18, y - 4, colour, 3)
            s.text(x + 24, y, label, 11)

    def x_axis(self, lo, hi, ticks, label, tick_labels=True):
        self.xlo, self.xhi = lo, hi
        s = self.svg
        for t in ticks:
            if t < lo or t > hi:
                continue                    # nice ticks may start below / end above the range
            x = self.xp(t)
            s.line(x, self.top, x, self.bottom, opacity=0.12)
            if tick_labels:
                s.text(x, self.bottom + 14, fmt(t), 10, "middle")
        s.line(self.left, self.bottom, self.right, self.bottom)
        s.text((self.left + self.right) / 2, self.bottom + 30, label, 11, "middle")

    def y_axis(self, lo, hi, ticks, label):
        self.ylo, self.yhi = lo, hi
        s = self.svg
        for t in ticks:
            if t < lo or t > hi:
                continue
            y = self.yp(t)
            s.line(self.left, y, self.right, y, opacity=0.12)
            s.text(self.left - 6, y + 4, fmt(t), 10, "end")
        s.line(self.left, self.top, self.left, self.bottom)
        s.text(16, (self.top + self.bottom) / 2, label, 11, "middle", rotate=-90)

    def xp(self, v):
        return self.left + (v - self.xlo) / (self.xhi - self.xlo) * (self.right - self.left)

    def yp(self, v):
        return self.bottom - (v - self.ylo) / (self.yhi - self.ylo) * (self.bottom - self.top)


# ---------------------------------------------------------------------------
# Charts

def most_common(values):
    """Most common value; on a tie the lower one."""
    c = Counter(values)
    best = max(c.values())
    return min(v for v in c if c[v] == best)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def cycle_charts(args, mode, test, per_model, colours):
    """per_model: {model: 8x8 [height][width]}"""
    models = sorted(per_model)
    ref = [[most_common([per_model[m][v][u] for m in models]) for u in range(8)] for v in range(8)]
    out = []

    # Shape: cycles against width, one line per height
    heights = [("h=%d" % (v + 1), shade(args.text_color, args.background_color, 0.25 + 0.75 * v / 7)) for v in range(8)]
    f = Frame(args, args.cycle_title, test, mode, [])
    top = max(max(r) for r in ref)
    yt = nice_ticks(0, top)
    f.x_axis(0.7, 8.6, range(1, 9), "Width (pixels/bytes)")
    f.y_axis(0, yt[-1], yt, "Z80 cycles")
    for v in range(8):
        pts = [(f.xp(u + 1), f.yp(ref[v][u])) for u in range(8)]
        f.svg.polyline(pts, heights[v][1], 2)
        for x, y in pts:
            f.svg.circle(x, y, 2.5, heights[v][1])
        f.svg.text(pts[-1][0] + 5, pts[-1][1] + 4, heights[v][0], 9)
    f.svg.text(f.right, f.top - 4, "most common value of %d models" % len(models), 9, "end")
    name = "cycles_%s_%s_shape.svg" % (slug(mode), slug(test))
    f.svg.save(os.path.join(args.output_dir, name))
    out.append(name)

    # Deviation from the most common value, one colour per model
    dev = {m: [[per_model[m][v][u] - ref[v][u] for u in range(8)] for v in range(8)] for m in models}
    lo = min(min(min(r) for r in dev[m]) for m in models)
    hi = max(max(max(r) for r in dev[m]) for m in models)
    lo, hi = min(lo, -1), max(hi, 1)
    f = Frame(args, args.cycle_title, test, mode, [(m, colours[m]) for m in models])
    gap = 1.5                                         # slots between height groups
    total = 64 + 7 * gap
    f.x_axis(-0.5, total - 0.5, [], "Size: width 1..8 within each height group", tick_labels=False)
    f.y_axis(lo - 0.5, hi + 0.5, range(lo, hi + 1), "Cycles vs. most common value")
    f.svg.line(f.left, f.yp(0), f.right, f.yp(0), opacity=0.5)
    slot = (f.right - f.left) / total
    for v in range(8):
        x0 = v * (8 + gap)
        f.svg.text(f.xp(x0 + 3.5), f.bottom + 14, "h=%d" % (v + 1), 10, "middle")
        if v:
            xs = f.xp(x0 - gap / 2 - 0.5)
            f.svg.line(xs, f.top, xs, f.bottom, opacity=0.25, dash="3,3")
        for u in range(8):
            for i, m in enumerate(models):
                off = (i - (len(models) - 1) / 2) * slot / (len(models) + 1)
                f.svg.circle(f.xp(x0 + u) + off, f.yp(dev[m][v][u]), 2.6, colours[m])
    name = "cycles_%s_%s_deviation.svg" % (slug(mode), slug(test))
    f.svg.save(os.path.join(args.output_dir, name))
    out.append(name)
    return out


def hist_chart(args, size, mode, test, per_model, colours):
    """per_model: {model: (minimum, [counts])}"""
    models = sorted(per_model)
    title = args.histogram_title.replace("n x n", "%d x %d" % size)
    f = Frame(args, title, test, mode, [(m, colours[m]) for m in models])

    series = {}
    for m in models:
        mn, counts = per_model[m]
        total = float(sum(counts)) or 1.0
        series[m] = [(mn + i, 100.0 * c / total) for i, c in enumerate(counts)]

    xlo = min(p[0] for s in series.values() for p in s) - 1
    xhi = max(p[0] for s in series.values() for p in s) + 1
    ytop = max(p[1] for s in series.values() for p in s)
    yt = nice_ticks(0, ytop)
    f.x_axis(xlo, xhi, nice_ticks(xlo, xhi, 6)[:-1] if xhi - xlo > 6 else range(xlo, xhi + 1), "Z80 cycles")
    f.xlo, f.xhi = xlo, xhi                           # ticks may extend the range: keep the data range
    f.y_axis(0, yt[-1], yt, "% of attempts")

    # Identical curves would hide each other: the first model gets the widest
    # line, each next one a thinner one, so overlaps show as nested colours.
    widths = {m: max(1.0, 1.0 + 0.8 * (len(models) - 1 - i)) for i, m in enumerate(models)}
    for m in models:
        # Steps: each delay value is one bar of width 1 cycle, starting and ending at 0
        pts = []
        s = series[m]
        pts.append((f.xp(s[0][0] - 0.5), f.yp(0)))
        for x, y in s:
            pts.append((f.xp(x - 0.5), f.yp(y)))
            pts.append((f.xp(x + 0.5), f.yp(y)))
        pts.append((f.xp(s[-1][0] + 0.5), f.yp(0)))
        f.svg.polyline(pts, colours[m], widths[m])

    name = "hist_%dx%d_%s_%s.svg" % (size[0], size[1], slug(mode), slug(test))
    f.svg.save(os.path.join(args.output_dir, name))
    return name


def shade(fg, bg, t):
    """Mix of fg and bg: t = 1 gives fg, t = 0 gives bg (#rrggbb or a few names)."""
    names = {"white": "#ffffff", "black": "#000000"}
    a = names.get(fg.lower(), fg).lstrip("#")
    b = names.get(bg.lower(), bg).lstrip("#")
    if len(a) != 6 or len(b) != 6:
        return fg
    ca = [int(a[i:i + 2], 16) for i in (0, 2, 4)]
    cb = [int(b[i:i + 2], 16) for i in (0, 2, 4)]
    return "#%02x%02x%02x" % tuple(int(cb[i] + (ca[i] - cb[i]) * t) for i in range(3))


# ---------------------------------------------------------------------------
# PNG conversion

BROWSER_PATHS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
]


def png_converter(width, height):
    """Returns a function svg_path -> png_path, or None if nothing can convert."""
    try:
        import cairosvg

        def convert(svg, png):
            cairosvg.svg2png(url=svg, write_to=png, output_width=width, output_height=height)
        return convert
    except ImportError:
        pass                                # not installed
    except OSError:                         # installed, but the Cairo library (e.g. libcairo-2.dll) is missing
        print("note: cairosvg can't load the Cairo library, using a browser instead")

    browser = None
    for name in ("chrome", "google-chrome", "chromium", "chromium-browser", "msedge"):
        browser = shutil.which(name)
        if browser:
            break
    if not browser:
        browser = next((b for b in BROWSER_PATHS if os.path.exists(b)), None)
    if not browser:
        return None

    def convert(svg, png):
        url = "file:///" + os.path.abspath(svg).replace("\\", "/")
        subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--window-size=%d,%d" % (width, height), "--screenshot=" + os.path.abspath(png), url],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        if not os.path.exists(png):
            raise RuntimeError("browser did not write " + png)
    return convert


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Generate SVG charts from cmdcycle reports.")
    ap.add_argument("--data-dir", default=".")
    ap.add_argument("--output-dir", default=".")
    ap.add_argument("--width", type=int, default=512, help="chart image width")
    ap.add_argument("--height", type=int, default=512, help="chart image height")
    ap.add_argument("--background-color", default="white")
    ap.add_argument("--text-color", default="black")
    ap.add_argument("--cycle-title", default="Cycle cost", help="cycle chart title")
    ap.add_argument("--histogram-title", default="Histogram n x n", help="histogram chart title ('n x n' becomes the size)")
    ap.add_argument("--png", action="store_true", help="write PNG instead of SVG (needs cairosvg, or Chrome/Edge)")
    args = ap.parse_args()

    convert = None
    if args.png:
        convert = png_converter(args.width, args.height)
        if convert is None:
            raise SystemExit("--png: install cairosvg (pip install cairosvg), or Chrome/Edge")

    os.makedirs(args.output_dir, exist_ok=True)

    files = sorted(glob.glob(os.path.join(args.data_dir, "*.md")) + glob.glob(os.path.join(args.data_dir, "*.MD")))
    files = sorted(set(os.path.normcase(os.path.abspath(f)) for f in files))
    reports = [r for r in (parse_report(f) for f in files) if r and r["mode"]]

    models = sorted({r["model"] for r in reports})
    colours = {m: PALETTE[i % len(PALETTE)] for i, m in enumerate(models)}

    cycles = defaultdict(dict)                        # (mode, test) -> {model: table}
    hists = defaultdict(dict)                         # (size, mode, test) -> {model: (min, counts)}
    for r in reports:
        if "cycles" in r:
            for test, table in r["cycles"].items():
                if r["model"] in cycles[(r["mode"], test)]:
                    print("warning: second cycle report for %s, %s, %s ignored: %s" % (r["model"], r["mode"], test, r["path"]))
                    continue
                cycles[(r["mode"], test)][r["model"]] = table
        elif r.get("size"):
            for test, data in r["hist"].items():
                key = (r["size"], r["mode"], test)
                if r["model"] in hists[key]:
                    print("warning: second histogram for %s, %s, %s ignored: %s" % (r["model"], r["mode"], test, r["path"]))
                    continue
                hists[key][r["model"]] = data

    def order(mode, test):
        return (MODE_ORDER.index(mode) if mode in MODE_ORDER else 99, TEST_ORDER.index(test_key(test)) if test_key(test) in TEST_ORDER else 99)

    written = []
    for (mode, test) in sorted(cycles, key=lambda k: order(*k)):
        written += cycle_charts(args, mode, test, cycles[(mode, test)], colours)
    for (size, mode, test) in sorted(hists, key=lambda k: (k[0],) + order(k[1], k[2])):
        written.append(hist_chart(args, size, mode, test, hists[(size, mode, test)], colours))

    if convert:
        for i, name in enumerate(written):
            svg = os.path.join(args.output_dir, name)
            png = svg[:-4] + ".png"
            if os.path.exists(png):
                os.remove(png)
            convert(svg, png)
            os.remove(svg)
            written[i] = name[:-4] + ".png"

    print("%d reports, %d models: %s" % (len(reports), len(models), ", ".join(models)))
    print("%d %s charts written to %s" % (len(written), "PNG" if convert else "SVG", os.path.abspath(args.output_dir)))


if __name__ == "__main__":
    main()

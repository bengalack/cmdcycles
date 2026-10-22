# Comparison of all measured machines, and openMSX

Cycle cost and 8x8 histograms of all machines in `data/`, per command and display mode, next
to the 8x8 histogram measured in openMSX (`data_openmsx/`, emulated Sony HB-F1XD,
openMSX version 21.0.-556-gae5998524).
One row per mode: Cycles, Deviation, Histogram 8x8 (real machines), Histogram 8x8 (openMSX).
Click a chart to open it in full size.

- **Cycles**: duration of each size (width 1..8, one line per height), most common value of all machines.
- **Deviation**: each machine's value minus that most common value, per size.
- **Histogram 8x8**: % of attempts against duration, one curve per machine.
    - Note a unstable, noisy line when there is clock drift (Turbo R)

Generated with `python scripts/gen_charts.py --data-dir data --output-dir img` and
`python scripts/gen_charts.py --data-dir data_openmsx --output-dir img_openmsx`.

## Copy: LMMM-TEOR

**Active display**

<div align="center">
<a href="img/cycles_active-display_copy-lmmm-teor_shape.svg"><img src="img/cycles_active-display_copy-lmmm-teor_shape.svg" width="22%" alt="Copy: LMMM-TEOR cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_copy-lmmm-teor_deviation.svg"><img src="img/cycles_active-display_copy-lmmm-teor_deviation.svg" width="22%" alt="Copy: LMMM-TEOR deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_copy-lmmm-teor.svg"><img src="img/hist_8x8_active-display_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_copy-lmmm-teor.svg"><img src="img_openmsx/hist_8x8_active-display_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_copy-lmmm-teor_shape.svg"><img src="img/cycles_no-sprites_copy-lmmm-teor_shape.svg" width="22%" alt="Copy: LMMM-TEOR cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_copy-lmmm-teor_deviation.svg"><img src="img/cycles_no-sprites_copy-lmmm-teor_deviation.svg" width="22%" alt="Copy: LMMM-TEOR deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_copy-lmmm-teor.svg"><img src="img/hist_8x8_no-sprites_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_copy-lmmm-teor.svg"><img src="img_openmsx/hist_8x8_no-sprites_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_copy-lmmm-teor_shape.svg"><img src="img/cycles_no-display_copy-lmmm-teor_shape.svg" width="22%" alt="Copy: LMMM-TEOR cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_copy-lmmm-teor_deviation.svg"><img src="img/cycles_no-display_copy-lmmm-teor_deviation.svg" width="22%" alt="Copy: LMMM-TEOR deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_copy-lmmm-teor.svg"><img src="img/hist_8x8_no-display_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_copy-lmmm-teor.svg"><img src="img_openmsx/hist_8x8_no-display_copy-lmmm-teor.svg" width="22%" alt="Copy: LMMM-TEOR histogram 8x8 openMSX, No display"></a>
</div>

## Copy: HMMM

**Active display**

<div align="center">
<a href="img/cycles_active-display_copy-hmmm_shape.svg"><img src="img/cycles_active-display_copy-hmmm_shape.svg" width="22%" alt="Copy: HMMM cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_copy-hmmm_deviation.svg"><img src="img/cycles_active-display_copy-hmmm_deviation.svg" width="22%" alt="Copy: HMMM deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_copy-hmmm.svg"><img src="img/hist_8x8_active-display_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_copy-hmmm.svg"><img src="img_openmsx/hist_8x8_active-display_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_copy-hmmm_shape.svg"><img src="img/cycles_no-sprites_copy-hmmm_shape.svg" width="22%" alt="Copy: HMMM cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_copy-hmmm_deviation.svg"><img src="img/cycles_no-sprites_copy-hmmm_deviation.svg" width="22%" alt="Copy: HMMM deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_copy-hmmm.svg"><img src="img/hist_8x8_no-sprites_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_copy-hmmm.svg"><img src="img_openmsx/hist_8x8_no-sprites_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_copy-hmmm_shape.svg"><img src="img/cycles_no-display_copy-hmmm_shape.svg" width="22%" alt="Copy: HMMM cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_copy-hmmm_deviation.svg"><img src="img/cycles_no-display_copy-hmmm_deviation.svg" width="22%" alt="Copy: HMMM deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_copy-hmmm.svg"><img src="img/hist_8x8_no-display_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_copy-hmmm.svg"><img src="img_openmsx/hist_8x8_no-display_copy-hmmm.svg" width="22%" alt="Copy: HMMM histogram 8x8 openMSX, No display"></a>
</div>

## Copy: YMMM

**Active display**

<div align="center">
<a href="img/cycles_active-display_copy-ymmm_shape.svg"><img src="img/cycles_active-display_copy-ymmm_shape.svg" width="22%" alt="Copy: YMMM cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_copy-ymmm_deviation.svg"><img src="img/cycles_active-display_copy-ymmm_deviation.svg" width="22%" alt="Copy: YMMM deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_copy-ymmm.svg"><img src="img/hist_8x8_active-display_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_copy-ymmm.svg"><img src="img_openmsx/hist_8x8_active-display_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_copy-ymmm_shape.svg"><img src="img/cycles_no-sprites_copy-ymmm_shape.svg" width="22%" alt="Copy: YMMM cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_copy-ymmm_deviation.svg"><img src="img/cycles_no-sprites_copy-ymmm_deviation.svg" width="22%" alt="Copy: YMMM deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_copy-ymmm.svg"><img src="img/hist_8x8_no-sprites_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_copy-ymmm.svg"><img src="img_openmsx/hist_8x8_no-sprites_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_copy-ymmm_shape.svg"><img src="img/cycles_no-display_copy-ymmm_shape.svg" width="22%" alt="Copy: YMMM cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_copy-ymmm_deviation.svg"><img src="img/cycles_no-display_copy-ymmm_deviation.svg" width="22%" alt="Copy: YMMM deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_copy-ymmm.svg"><img src="img/hist_8x8_no-display_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_copy-ymmm.svg"><img src="img_openmsx/hist_8x8_no-display_copy-ymmm.svg" width="22%" alt="Copy: YMMM histogram 8x8 openMSX, No display"></a>
</div>

## Fill: HMMV

**Active display**

<div align="center">
<a href="img/cycles_active-display_fill-hmmv_shape.svg"><img src="img/cycles_active-display_fill-hmmv_shape.svg" width="22%" alt="Fill: HMMV cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_fill-hmmv_deviation.svg"><img src="img/cycles_active-display_fill-hmmv_deviation.svg" width="22%" alt="Fill: HMMV deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_fill-hmmv.svg"><img src="img/hist_8x8_active-display_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_fill-hmmv.svg"><img src="img_openmsx/hist_8x8_active-display_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_fill-hmmv_shape.svg"><img src="img/cycles_no-sprites_fill-hmmv_shape.svg" width="22%" alt="Fill: HMMV cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_fill-hmmv_deviation.svg"><img src="img/cycles_no-sprites_fill-hmmv_deviation.svg" width="22%" alt="Fill: HMMV deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_fill-hmmv.svg"><img src="img/hist_8x8_no-sprites_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_fill-hmmv.svg"><img src="img_openmsx/hist_8x8_no-sprites_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_fill-hmmv_shape.svg"><img src="img/cycles_no-display_fill-hmmv_shape.svg" width="22%" alt="Fill: HMMV cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_fill-hmmv_deviation.svg"><img src="img/cycles_no-display_fill-hmmv_deviation.svg" width="22%" alt="Fill: HMMV deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_fill-hmmv.svg"><img src="img/hist_8x8_no-display_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_fill-hmmv.svg"><img src="img_openmsx/hist_8x8_no-display_fill-hmmv.svg" width="22%" alt="Fill: HMMV histogram 8x8 openMSX, No display"></a>
</div>

## Fill: LMMV

**Active display**

<div align="center">
<a href="img/cycles_active-display_fill-lmmv_shape.svg"><img src="img/cycles_active-display_fill-lmmv_shape.svg" width="22%" alt="Fill: LMMV cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_fill-lmmv_deviation.svg"><img src="img/cycles_active-display_fill-lmmv_deviation.svg" width="22%" alt="Fill: LMMV deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_fill-lmmv.svg"><img src="img/hist_8x8_active-display_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_fill-lmmv.svg"><img src="img_openmsx/hist_8x8_active-display_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_fill-lmmv_shape.svg"><img src="img/cycles_no-sprites_fill-lmmv_shape.svg" width="22%" alt="Fill: LMMV cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_fill-lmmv_deviation.svg"><img src="img/cycles_no-sprites_fill-lmmv_deviation.svg" width="22%" alt="Fill: LMMV deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_fill-lmmv.svg"><img src="img/hist_8x8_no-sprites_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_fill-lmmv.svg"><img src="img_openmsx/hist_8x8_no-sprites_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_fill-lmmv_shape.svg"><img src="img/cycles_no-display_fill-lmmv_shape.svg" width="22%" alt="Fill: LMMV cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_fill-lmmv_deviation.svg"><img src="img/cycles_no-display_fill-lmmv_deviation.svg" width="22%" alt="Fill: LMMV deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_fill-lmmv.svg"><img src="img/hist_8x8_no-display_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_fill-lmmv.svg"><img src="img_openmsx/hist_8x8_no-display_fill-lmmv.svg" width="22%" alt="Fill: LMMV histogram 8x8 openMSX, No display"></a>
</div>

## Line: LINE-TEOR

**Active display**

<div align="center">
<a href="img/cycles_active-display_line-line-teor_shape.svg"><img src="img/cycles_active-display_line-line-teor_shape.svg" width="22%" alt="Line: LINE-TEOR cycles, Active display"></a>&nbsp;&nbsp;<a href="img/cycles_active-display_line-line-teor_deviation.svg"><img src="img/cycles_active-display_line-line-teor_deviation.svg" width="22%" alt="Line: LINE-TEOR deviation, Active display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_active-display_line-line-teor.svg"><img src="img/hist_8x8_active-display_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8, Active display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_active-display_line-line-teor.svg"><img src="img_openmsx/hist_8x8_active-display_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8 openMSX, Active display"></a>
</div>

**No sprites**

<div align="center">
<a href="img/cycles_no-sprites_line-line-teor_shape.svg"><img src="img/cycles_no-sprites_line-line-teor_shape.svg" width="22%" alt="Line: LINE-TEOR cycles, No sprites"></a>&nbsp;&nbsp;<a href="img/cycles_no-sprites_line-line-teor_deviation.svg"><img src="img/cycles_no-sprites_line-line-teor_deviation.svg" width="22%" alt="Line: LINE-TEOR deviation, No sprites"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-sprites_line-line-teor.svg"><img src="img/hist_8x8_no-sprites_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8, No sprites"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-sprites_line-line-teor.svg"><img src="img_openmsx/hist_8x8_no-sprites_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8 openMSX, No sprites"></a>
</div>

**No display**

<div align="center">
<a href="img/cycles_no-display_line-line-teor_shape.svg"><img src="img/cycles_no-display_line-line-teor_shape.svg" width="22%" alt="Line: LINE-TEOR cycles, No display"></a>&nbsp;&nbsp;<a href="img/cycles_no-display_line-line-teor_deviation.svg"><img src="img/cycles_no-display_line-line-teor_deviation.svg" width="22%" alt="Line: LINE-TEOR deviation, No display"></a>&nbsp;&nbsp;<a href="img/hist_8x8_no-display_line-line-teor.svg"><img src="img/hist_8x8_no-display_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8, No display"></a>&nbsp;&nbsp;<a href="img_openmsx/hist_8x8_no-display_line-line-teor.svg"><img src="img_openmsx/hist_8x8_no-display_line-line-teor.svg" width="22%" alt="Line: LINE-TEOR histogram 8x8 openMSX, No display"></a>
</div>

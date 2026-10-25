# Tube model fits from data sheets

Scripts used to fit the Koren triode model in `engine/include/sas/devices.h` to a data sheet. They are
development tools (Python with numpy and OpenCV; there is no scipy, the least-squares is a small
Levenberg-Marquardt in `koren_fit_lib.py`) and are not built or tested with the plugin.

12AX7 (`TriodeModel::ecc83Fit`, netlist model `12AX7`):

1. Render page 3 of the GE 12AX7 data sheet ET-T509B (average plate characteristics) at 600 dpi and crop the
   upper plot to `ge_plate_crop.png`.
2. `extract_ge_12ax7.py` keeps the thick curve strokes (the grid is thinner), splits the 0 V and -0.5 V curves
   that touch at the origin, and writes `ge_curves_v2.npy` (plate volts, mA, for each grid voltage).
3. `fit_12ax7.py` fits mu, kg1, kp, kvb with the exponent fixed (1.3; an exponent below 1 breaks Newton at the
   cutoff) and prints the data sheet check (1.2 mA, 1.6 mA/V, 62.5k at 250 V / -2 V).

Result with ex 1.3: mu 104.7, kg1 474, kp 745, kvb 13963, relative rms error 14 % against 53 % for the stock
Koren values, 1.17 mA / 1.84 mA/V / 55k at the typical point. The Twin schematic's cathode voltages (TP2 / TP5
about 1.9 to 2.0 V) are reproduced (1.99 V) where the stock values gave 1.69 V.

## Other tubes (second round)

- 12AT7 (`ecc81Fit`, model `12AT7`): the Mullard ECC81 sheet is a vector PDF; `extract_mullard_12at7.py` reads the
  curve outlines straight from the drawing (`pdfcurves.py` samples the Bezier paths) and `fit_triode.py` fits them
  (8 % rms, 8.1 mA / 4.4 mA/V / 12.5k at 250 V / -2 V).
- KT88 / 6550 (`el6550Fit`, model `6550DS`): the JJ sheet plots (Ug2 300 V) are vector too (`extract_jj_el34_kt88.py`);
  `fit_pentodes.py` fixes mu to the sheet's 8 and the exponent to 1.35 and adds the typical point (140 mA, 11.5 mA/V).
  8 % rms against 17 % for the stock values; the AFD100 power amp scale went from 0.9 to 1.03 to keep ~105 W.
- EL34: NOT changed. `fit_el34_check.py` shows a fit to the 250 V plate curves alone makes a JCM800 give 170 W, and that the
  stock model (kg1 / kg2 scaled 1.3) already reproduces the sheet's transfer curves at Ug2 375 / 425 V within 6 %, which
  is where the amps run.
- 6L6GC: NOT changed. The only chart (TAD, raster, Ug2 = Ua) is fitted no better than 18 % by the Koren pentode
  (`extract_tad_6l6gc.py`, `fit_6l6gc_tad.py`); the stock values give 56 mA where the sheet says 54 mA.

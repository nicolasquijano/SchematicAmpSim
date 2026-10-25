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

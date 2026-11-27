#!/usr/bin/env python3
"""MCP server (stdio, standard library only) for Schematic Amp Sim.

It lets an AI list the amps, render a guitar recording through one amp setting with tools/sas_cli, measure the result
(level, tone balance per band) against a reference, and save the settings it settles on as a tone file.
The AI does not hear: it measures. The final judgement is the user's ears.

Environment:
  SAS_CLI        path of sas_cli.exe (default: build/tools/sas_cli.exe of this repository)
  SAS_TONES_DIR  where renders and saved tones go (default: ~/Documents/SAS-tones)
"""
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CLI = os.environ.get("SAS_CLI") or os.path.join(REPO, "build", "tools", "sas_cli.exe")
TONES = os.environ.get("SAS_TONES_DIR") or os.path.join(os.path.expanduser("~"), "Documents", "SAS-tones")
RENDERS = os.path.join(TONES, "renders")
PROTOCOL = "2024-11-05"


def run_cli(args, timeout=300):
    if not os.path.isfile(CLI):
        raise RuntimeError("sas_cli.exe not found at %s (build it: build.cmd target sas_cli)" % CLI)
    p = subprocess.run([CLI] + args, capture_output=True, text=True, timeout=timeout)
    out = p.stdout.strip()
    try:
        data = json.loads(out)
    except ValueError:
        raise RuntimeError("sas_cli gave no JSON (exit %d): %s %s" % (p.returncode, out[:300], p.stderr[:300]))
    if isinstance(data, dict) and "error" in data:
        raise RuntimeError(data["error"])
    return data


def safe_name(s):
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", s or "").strip("._")
    return s or "tone"


def render_args(a):
    args = ["render", "--amp", a["amp"]]
    if a.get("preset"):
        args += ["--preset", a["preset"]]
    if a.get("jack"):
        args += ["--jack", str(a["jack"])]
    for k, v in (a.get("params") or {}).items():
        args += ["--set", "%s=%s" % (k, v)]
    for k, v in (a.get("selectors") or {}).items():
        args += ["--sel", "%s=%s" % (k, v)]
    for k, v in (a.get("voicings") or {}).items():
        args += ["--voicing", "%s=%s" % (k, v)]
    for key, opt in (("input_db", "input-db"), ("output_db", "output-db"), ("quality", "quality"), ("cab", "cab"),
                     ("noise", "noise"), ("seconds", "seconds"), ("input_wav", "in"), ("reference_wav", "ref")):
        if a.get(key) is not None:
            args += ["--" + opt, str(a[key])]
    ts = a.get("overdrive")
    if ts:
        args += ["--ts", "%s,%s,%s" % (ts.get("drive", 5), ts.get("tone", 5), ts.get("level", 5))]
    return args


def tool_list_amps(a):
    return run_cli(["list"])


def tool_amp_info(a):
    return run_cli(["info", a["amp"]])


def tool_render(a):
    args = render_args(a)
    os.makedirs(RENDERS, exist_ok=True)
    name = safe_name(a.get("name")) + "_" + time.strftime("%H%M%S") + ".wav"
    path = os.path.join(RENDERS, name)
    args += ["--out", path]
    result = run_cli(args)
    result["note"] = "Levels are dBFS; balance/bands are dB relative to the total power. The audio file is in 'out'."
    return result


def tool_analyze(a):
    args = ["analyze", a["file"]]
    if a.get("reference"):
        args += ["--ref", a["reference"]]
    return run_cli(args)


def tool_save_tone(a):
    os.makedirs(TONES, exist_ok=True)
    path = os.path.join(TONES, safe_name(a["name"]) + ".json")
    data = {k: a[k] for k in ("name", "description", "amp", "preset", "jack", "params", "selectors", "input_db", "output_db",
                              "quality", "overdrive", "cab", "notes") if k in a}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    return {"saved": path}


def tool_list_tones(a):
    if not os.path.isdir(TONES):
        return []
    out = []
    for fn in sorted(os.listdir(TONES)):
        if fn.endswith(".json"):
            with open(os.path.join(TONES, fn), encoding="utf-8") as f:
                out.append(json.load(f))
    return out


SETTINGS = {
    "amp": {"type": "string", "description": "Amp id from sas_list_amps (e.g. jtm45, plexi_1959, mark_v)."},
    "preset": {"type": "string", "description": "Factory preset name of the amp (see sas_amp_info), applied first."},
    "jack": {"type": "string", "description": "Input jack / channel name (see sas_amp_info 'jacks')."},
    "params": {"type": "object", "description": "Controls on the 0..10 scale by param name, e.g. {\"bass\": 4, \"treble\": 7, \"presence\": 6}. Overrides the preset.",
               "additionalProperties": {"type": "number"}},
    "selectors": {"type": "object", "description": "Switch positions by selector name -> position index (see sas_amp_info 'selectors').",
                  "additionalProperties": {"type": "integer"}},
    "voicings": {"type": "object", "description": "Voicings of the amp's preamp and power amp by name -> position index (see sas_amp_info 'preamp_voicings' and 'power_voicings'; 0 = as drawn). Power-amp voicings are heard in the Normal, High and Efficient qualities only.",
                 "additionalProperties": {"type": "integer"}},
    "input_db": {"type": "number", "description": "Gain at the input in dB (guitar level)."},
    "output_db": {"type": "number", "description": "Output gain in dB."},
    "quality": {"type": "string", "enum": ["normal", "high", "lite", "eco", "efficient"], "description": "Solver quality; lite/eco are much faster and close."},
    "overdrive": {"type": "object", "description": "Tube-Screamer style pedal in front of the amp, knobs 0..10: {drive, tone, level}.",
                  "properties": {"drive": {"type": "number"}, "tone": {"type": "number"}, "level": {"type": "number"}}},
    "cab": {"type": "string", "enum": ["on", "off"], "description": "Cabinet filter (default on). Cabinet impulse responses and the effects rack are not part of this tool."},
}

TOOLS = [
    ("sas_list_amps", "List the amps (id, name, category).", {"type": "object", "properties": {}}, tool_list_amps),
    ("sas_amp_info", "Controls (param names, defaults, 0..10 scale), jacks, selectors and factory presets of one amp. Call it before setting params.",
     {"type": "object", "properties": {"amp": SETTINGS["amp"]}, "required": ["amp"]}, tool_amp_info),
    ("sas_render",
     "Render a guitar recording (input_wav, or a built-in clean riff when omitted) through one amp setting and measure it: rms/peak, crest factor, "
     "spectral centroid, tone balance per region and per 1/3-octave band. With reference_wav it also returns the per-band difference to that reference "
     "(positive = this render has more of that region). Writes the rendered WAV and returns its path. Use it in a loop: render, compare, adjust params.",
     {"type": "object", "properties": dict(SETTINGS, **{
         "input_wav": {"type": "string", "description": "Absolute path of a DI guitar WAV (mono or stereo, any rate). Sample values are read as volts at the amp input."},
         "reference_wav": {"type": "string", "description": "Absolute path of a WAV with the target tone, for the comparison."},
         "seconds": {"type": "number", "description": "Maximum length to render."},
         "name": {"type": "string", "description": "Label for the output file name."}}), "required": ["amp"]}, tool_render),
    ("sas_analyze", "Measure a WAV file (same metrics as sas_render), optionally against a reference WAV.",
     {"type": "object", "properties": {"file": {"type": "string"}, "reference": {"type": "string"}}, "required": ["file"]}, tool_analyze),
    ("sas_save_tone", "Save the settings you settled on (amp, preset, params, selectors, jack, overdrive...) as a tone file, with a short description.",
     {"type": "object", "properties": dict(SETTINGS, **{"name": {"type": "string"}, "description": {"type": "string"}, "notes": {"type": "string"}}),
      "required": ["name", "amp"]}, tool_save_tone),
    ("sas_list_tones", "List the saved tones.", {"type": "object", "properties": {}}, tool_list_tones),
]


def respond(msg_id, result=None, error=None):
    m = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        m["error"] = error
    else:
        m["result"] = result
    sys.stdout.write(json.dumps(m) + "\n")
    sys.stdout.flush()


def handle(msg):
    method, mid = msg.get("method"), msg.get("id")
    if method == "initialize":
        respond(mid, {"protocolVersion": msg.get("params", {}).get("protocolVersion", PROTOCOL), "capabilities": {"tools": {}},
                      "serverInfo": {"name": "schematic-amp-sim", "version": "0.1.0"}})
    elif method == "ping":
        respond(mid, {})
    elif method == "tools/list":
        respond(mid, {"tools": [{"name": n, "description": d, "inputSchema": s} for n, d, s, _ in TOOLS]})
    elif method == "tools/call":
        p = msg.get("params", {})
        fn = next((t[3] for t in TOOLS if t[0] == p.get("name")), None)
        if not fn:
            respond(mid, error={"code": -32602, "message": "unknown tool %s" % p.get("name")})
            return
        try:
            res = fn(p.get("arguments") or {})
            respond(mid, {"content": [{"type": "text", "text": json.dumps(res, ensure_ascii=False, indent=1)}]})
        except Exception as e:  # reported to the model, which can correct the call
            respond(mid, {"content": [{"type": "text", "text": "error: %s" % e}], "isError": True})
    elif mid is not None:
        respond(mid, error={"code": -32601, "message": "method not found: %s" % method})


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            handle(json.loads(line))
        except Exception as e:
            sys.stderr.write("sas_mcp: %s\n" % e)


if __name__ == "__main__":
    main()

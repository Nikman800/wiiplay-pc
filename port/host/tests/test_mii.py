#!/usr/bin/env python3
"""A Mii from the console's Mii database, its record saved and shown again (port/docs/HOST_BUILD.md §40).

`make miitest` (64-bit): runs of build/host64/boot on lavapipe, one after the other, on one
NAND directory. The Mii database is written here (tests/mii_db.py: one Mii, "Tester", made
from parameters, with a create ID as the Mii Channel makes them) and given with `--mii-db`.

  bad   `--mii-db` with a file that is not a database (a damaged CRC): the run stops at once
        (status 1, a message) and the NAND gets no database;
  first a fresh NAND: the menus with the Mii (tests/boot/menus_mii.txt: Select Mii opens on
        the "Wii Console" tab, the Mii, "Added Tester to the player list", "Play with this
        Mii.") and a whole Shooting Range (tests/boot/shooting.txt) to the results and back;
  again the same NAND (the save of `first`): tests/boot/menus_mii_saved.txt to the party
        menu with the same Mii, which the game finds on its player list (no message).

Checks: the scenes; the database imported byte for byte and never written by the game; the
save after `first`: the Mii registered on the player list (slot 0, its create ID) with a
Shooting Range record (the final score the game reached, read by host_probe); after `again`
the same slot, create ID and records (RPSysPlayer::loadData resets a slot whose create ID is
not the Mii's, so a Mii the game did not recognise would lose its record); the face in Select
Mii, "Play with this Mii." and the party menu drawn (skin-coloured pixels where the head is); the party menu's "Record: N points" box (Shooting Range highlighted, the pointer
in the same place) different in `again` from `first` (0 points); the log.
"""

import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mii_db  # noqa: E402
import test_boot as tb  # noqa: E402
import test_sys  # noqa: E402

FIRST_FRAMES = 16200
FIRST_SHOTS = [(2690, "select_mii"), (3100, "added"), (3500, "play_with_mii"), (4790, "party_menu"),
               (15740, "results_menu"), (16190, "back_to_menu")]
AGAIN_FRAMES = 2400
AGAIN_SHOTS = [(1290, "select_mii"), (1690, "play_with_mii"), (2390, "party_menu")]
FACE_BOXES = {"select_mii": (60, 150, 170, 250), "play_with_mii": (270, 140, 370, 240),
              "party_menu": (280, 140, 370, 230)}
RECORD_BOX = (360, 352, 610, 378)  # "Record: N points" on the party menu
SAVE = os.path.join("title", "00010000", "52484145", "data", "RPParty.dat")
DB = os.path.join("shared2", "menu", "FaceLib", "RFL_DB.dat")


def skin_pixels(rgba, box):
    x0, y0, x1, y1 = box
    n = 0
    for y in range(y0, y1):
        row = rgba[(y * tb.W + x0) * 4:(y * tb.W + x1) * 4]
        for i in range(0, len(row), 4):
            r, g, b = row[i], row[i + 1], row[i + 2]
            if r > 180 and r > g + 15 and g > b + 15:
                n += 1
    return n


def crop(rgba, box):
    x0, y0, x1, y1 = box
    return b"".join(rgba[(y * tb.W + x0) * 4:(y * tb.W + x1) * 4] for y in range(y0, y1))


def strip_info(stderr):
    """The boot's one informational line for --mii-db (check_log takes boot: lines as errors)."""
    return "\n".join(l for l in stderr.splitlines() if not l.startswith("boot: Mii database: "))


def player0(nand):
    with open(os.path.join(nand, SAVE), "rb") as f:
        s = test_sys.decode_save(f.read())
    return s, s["players"][0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", required=True)
    ap.add_argument("--disc", required=True, help="extracted disc files directory")
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--gpu", action="store_true", help="use the real Vulkan driver, not lavapipe")
    ap.add_argument("--png", default=None, help="directory for PNGs of the screens checked")
    args = ap.parse_args()

    env = dict(os.environ)
    if not args.gpu:
        icd = tb.test_gxlyt.lavapipe_icd()
        if icd is None:
            raise SystemExit("FAIL: lavapipe (mesa-vulkan-drivers) not found")
        env["VK_ICD_FILENAMES"] = icd
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    env.setdefault("LP_NUM_THREADS", "4")
    scratch = os.path.join(args.scratch, "mii")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch, exist_ok=True)
    repo = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    ids = tb.scene_ids(repo)
    errs = []

    cid = mii_db.create_id(0)
    db = mii_db.database([(0, mii_db.char_data("Tester", cid, creator="Test"))])
    db_path = os.path.join(scratch, "RFL_DB.dat")
    with open(db_path, "wb") as f:
        f.write(db)

    # --- bad: a damaged database is refused before the game starts -------------------------
    bad_path = os.path.join(scratch, "bad_RFL_DB.dat")
    with open(bad_path, "wb") as f:
        f.write(db[:0x100] + bytes([db[0x100] ^ 1]) + db[0x101:])
    bad_nand = os.path.join(scratch, "bad", "nand")
    os.makedirs(bad_nand)
    bad = tb.subprocess.run([args.boot, "--disc", args.disc, "--nand", bad_nand, "--data-dir",
                            os.path.join(scratch, "bad", "data"), "--mii-db", bad_path, "--frames", "1"],
                            env=env, capture_output=True, text=True, timeout=600)
    if bad.returncode == 0 or "checksum" not in bad.stderr or os.path.exists(os.path.join(bad_nand, DB)):
        errs.append("bad: status %d, stderr %r" % (bad.returncode, bad.stderr[-300:]))

    # --- first: a fresh NAND, the Mii, a whole Shooting Range ------------------------------
    first_script = tb.script(scratch, "menus_mii_shooting.txt", ["menus_mii.txt", "shooting.txt"])
    shots1 = [(f, os.path.join(scratch, "first_%s.rgba" % n)) for f, n in FIRST_SHOTS]
    extra = ["--frames", str(FIRST_FRAMES), "--input", first_script, "--mii-db", db_path]
    for f, path in shots1:
        extra += ["--save", "%d:%s" % (f, path)]
    d, fm1, sc1, end1, err1 = tb.run(args.boot, args.disc, scratch, "first", env, extra, timeout=7200)
    nand = os.path.join(d, "nand")
    party = ids["ESceneID_RPPartyMainScene"]
    want = [-1, ids["ESceneID_RPSysBootScene"], ids["ESceneID_RPPartyPackTitleScene"],
            ids["ESceneID_RPSysPlayerSelectScene"], party, ids["ESceneID_RPDucScene"], party]
    if [s for _, s in sc1] != want or len(fm1) != FIRST_FRAMES:
        errs.append("first: scenes %s over %d frames" % ([s for _, s in sc1], len(fm1)))
    tb.check_log(strip_info(err1), errs, "first")
    if "boot: Mii database: %s" % db_path not in err1:
        errs.append("first: no import line")
    with open(os.path.join(nand, DB), "rb") as f:
        if f.read() != db:
            errs.append("first: the NAND's RFL_DB.dat is not the database given")
    duc = tb.game_values(fm1, "duc")
    score = max(v[2] for _, v in duc) if duc else 0
    save1, p1 = player0(nand)
    rec1, medal1 = p1["records"][0]
    if p1["id"] != cid or not p1["flags"] & 2:
        errs.append("first: player 0 is %s (flags %X), not the Mii %s" % (p1["id"].hex(), p1["flags"], cid.hex()))
    if save1["crc"] != save1["crc_model"]:
        errs.append("first: save checksum")
    if score <= 0 or rec1[0] != score:
        errs.append("first: Shooting Range record %s medal %d, the game's score %d" % (rec1, medal1, score))
    others = [i for i, p in enumerate(save1["players"]) if i and any(p["id"])]
    if others:
        errs.append("first: other players on the list: %s" % others)

    # --- again: the same NAND, the same Mii --------------------------------------------------
    shots2 = [(f, os.path.join(scratch, "again_%s.rgba" % n)) for f, n in AGAIN_SHOTS]
    extra = ["--frames", str(AGAIN_FRAMES), "--input", os.path.join(HERE, "boot", "menus_mii_saved.txt")]
    for f, path in shots2:
        extra += ["--save", "%d:%s" % (f, path)]
    d2, fm2, sc2, end2, err2 = tb.run(args.boot, args.disc, scratch, "again", env,
                                      extra + ["--nand", nand], timeout=3600)
    want2 = [-1, ids["ESceneID_RPSysBootScene"], ids["ESceneID_RPPartyPackTitleScene"],
             ids["ESceneID_RPSysPlayerSelectScene"], party]
    if [s for _, s in sc2] != want2 or len(fm2) != AGAIN_FRAMES:
        errs.append("again: scenes %s over %d frames" % ([s for _, s in sc2], len(fm2)))
    tb.check_log(err2, errs, "again")
    save2, p2 = player0(nand)
    if (p2["id"], p2["flags"], p2["records"]) != (p1["id"], p1["flags"], p1["records"]):
        errs.append("again: player 0 changed: %s" % p2)
    with open(os.path.join(nand, DB), "rb") as f:
        if f.read() != db:
            errs.append("again: RFL_DB.dat was written")

    # --- the screens ----------------------------------------------------------------------------
    pics = {"first_" + n: tb.load_rgba(p) for (_, n), (_, p) in zip(FIRST_SHOTS, shots1)}
    pics.update({"again_" + n: tb.load_rgba(p) for (_, n), (_, p) in zip(AGAIN_SHOTS, shots2)})
    faces = {}
    for name, rgba in pics.items():
        kind = name.split("_", 1)[1]
        if kind in FACE_BOXES:
            faces[name] = skin_pixels(rgba, FACE_BOXES[kind])
            if faces[name] < 1500:
                errs.append("%s: %d skin-coloured pixels where the face is" % (name, faces[name]))
    for (f, n), (_, p) in zip(FIRST_SHOTS, shots1):
        scene = fm1[f][3]
        if n in ("select_mii", "added", "play_with_mii") and scene != ids["ESceneID_RPSysPlayerSelectScene"]:
            errs.append("first: %s in scene %d" % (n, scene))
    if crop(pics["again_party_menu"], RECORD_BOX) == crop(pics["first_party_menu"], RECORD_BOX):
        errs.append("again: the party menu's record box as in the first run (0 points)")
    if args.png:
        os.makedirs(args.png, exist_ok=True)
        for name, rgba in pics.items():
            tb.gxtex.write_png(os.path.join(args.png, "mii_%s.png" % name), tb.W, tb.H, tb.opaque(rgba))

    print("test_mii:")
    print("  bad database refused: status %d" % bad.returncode)
    print("  first: scenes %s over %d frames; Mii %s on the player list (slot 0); Shooting Range "
          "score %d saved as its record" % (" -> ".join(str(s) for _, s in sc1), len(fm1), cid.hex(), score))
    print("  again: scenes %s over %d frames; same slot, create ID and records; the record shown "
          "on the party menu; RFL_DB.dat never written" % (" -> ".join(str(s) for _, s in sc2), len(fm2)))
    print("  faces (skin-coloured pixels): %s" % ", ".join("%s %d" % kv for kv in sorted(faces.items())))
    if errs:
        for e in errs:
            print("FAIL: " + e)
        raise SystemExit(1)
    print("test_mii: OK")


if __name__ == "__main__":
    main()

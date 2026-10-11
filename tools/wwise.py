"""Resolve Space Marine 2 Wwise event names to the .wem media they play, reading the player's own paks.

Event id = FNV-1 32 of the lowercase name. HIRC objects reference children and media by 32-bit id; we walk
event -> actions -> sounds/containers by scanning each object's body for ids we know (objects or media).
Usage: python wwise.py <SM2 root> <bank> <event> [<event> ...]
"""
import struct, sys, zipfile, io, os

PAKDIR = r"client_pc\root\paks\client\default"


def fnv1(name):
    h = 2166136261
    for c in name.lower().encode():
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= c
    return h


def find_in_paks(root, member):
    d = os.path.join(root, PAKDIR)
    for f in sorted(os.listdir(d)):
        if not f.startswith("default_sound") or not f.endswith(".pak"):
            continue
        z = zipfile.ZipFile(os.path.join(d, f))
        if member in z.namelist():
            return z
    return None


class Bank:
    def __init__(self, data):
        self.objs = {}      # id -> (type, body)
        self.media = {}     # id -> (offset, size) inside DATA
        self.data = b""
        p = 0
        while p + 8 <= len(data):
            tag, size = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
            body = data[p + 8:p + 8 + size]
            if tag == b"DIDX":
                for o in range(0, size, 12):
                    mid, off, sz = struct.unpack_from("<III", body, o)
                    self.media[mid] = (off, sz)
            elif tag == b"DATA":
                self.data = body
            elif tag == b"HIRC":
                n = struct.unpack_from("<I", body, 0)[0]
                q = 4
                for _ in range(n):
                    t = body[q]
                    sz = struct.unpack_from("<I", body, q + 1)[0]
                    oid = struct.unpack_from("<I", body, q + 5)[0]
                    self.objs[oid] = (t, body[q + 9:q + 5 + sz])
                    q += 5 + sz
            p += 8 + size


NODE_TYPES = (2, 5, 6, 7, 9)  # sound, random/sequence, switch, actor-mixer, layer


def children_map(bank):
    """Parent -> children, from each node's DirectParentID (the first node id in its body)."""
    if hasattr(bank, "_kids"):
        return bank._kids
    kids = {}
    for oid, (t, body) in bank.objs.items():
        if t not in NODE_TYPES:
            continue
        for o in range(0, len(body) - 3):
            v = struct.unpack_from("<I", body, o)[0]
            if v != oid and v in bank.objs and bank.objs[v][0] in (5, 6, 7, 9):
                kids.setdefault(v, []).append(oid)
                break
    bank._kids = kids
    return kids


def resolve(bank, streamed_ids, event):
    """Return the media ids reachable from an event (embedded or streamed), walking only downward."""
    eid = fnv1(event)
    if eid not in bank.objs:
        return None
    known_media = set(bank.media) | set(streamed_ids)
    kids = children_map(bank)
    _, ebody = bank.objs[eid]
    todo = []
    for o in range(0, len(ebody) - 3):
        a = struct.unpack_from("<I", ebody, o)[0]
        if a in bank.objs and bank.objs[a][0] == 3:
            target = struct.unpack_from("<I", bank.objs[a][1], 2)[0]  # action: u16 type, u32 target
            todo.append(target)
    seen, out = set(), []
    while todo:
        oid = todo.pop()
        if oid in seen or oid not in bank.objs:
            continue
        seen.add(oid)
        t, body = bank.objs[oid]
        if t == 2:
            mid = struct.unpack_from("<I", body, 5)[0]  # plugin u32, stream type u8, source id u32
            if mid in known_media and mid not in out:
                out.append(mid)
        elif t in NODE_TYPES:
            todo.extend(kids.get(oid, []))
    return out


def shot_layers(bank, streamed, event):
    """A gun shot as layers of alternative takes: [[media, ...], ...]; one take per layer, mixed, is one shot.
    Uses the event's first Play target (the gun's own sound); later targets are shared room tails."""
    eid = fnv1(event)
    if eid not in bank.objs:
        return None
    kids = children_map(bank)
    _, ebody = bank.objs[eid]
    target = None
    for a in struct.unpack_from("<%dI" % ebody[0], ebody, 1):
        typ, tgt = struct.unpack_from("<HI", bank.objs[a][1], 0)
        if typ == 0x0403 and tgt in bank.objs:
            target = tgt
            break
    known = set(bank.media) | set(streamed)

    def takes(o):  # every media under o (alternatives)
        t, b = bank.objs[o]
        if t == 2:
            m = struct.unpack_from("<I", b, 5)[0]
            return [m] if m in known else []
        return [m for c in kids.get(o, []) for m in takes(c)]

    def layers(o):
        t, b = bank.objs[o]
        ch = kids.get(o, [])
        if t == 9:  # layer container: every child plays at once
            return [l for c in ch for l in layers(c)]
        if t == 5 and len(ch) == 1:
            return layers(ch[0])
        if t == 5 and ch and all(bank.objs[c][0] == 9 for c in ch):  # alternative layered shots: take the first
            return layers(ch[0])
        tk = takes(o)
        return [tk] if tk else []

    return layers(target) if target else None


def load(root, bank_name):
    member = f"sounds/desktop/{bank_name}.bnk"
    z = find_in_paks(root, member)
    bank = Bank(z.read(member))
    streamed = {}
    zmember = f"sounds/desktop/{bank_name}.zip"
    zz = find_in_paks(root, zmember)
    if zz:
        inner = zipfile.ZipFile(io.BytesIO(zz.read(zmember)))
        for n in inner.namelist():
            if n.endswith(".wem"):
                streamed[int(n[:-4])] = (inner, n)
    return bank, streamed


if __name__ == "__main__":
    root, bank_name = sys.argv[1], sys.argv[2]
    bank, streamed = load(root, bank_name)
    print(f"{bank_name}: {len(bank.objs)} objects, {len(bank.media)} embedded, {len(streamed)} streamed")
    for ev in sys.argv[3:]:
        r = resolve(bank, streamed, ev)
        print(ev, r if r is None else [(m, "emb" if m in bank.media else "str") for m in r])

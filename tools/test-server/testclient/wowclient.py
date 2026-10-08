"""Minimal headless WotLK 3.3.5a (build 12340) client for AzerothCore smoke tests.

Covers only what the housing tests need: SRP6 login, realm list, world auth,
character create/login, chat commands, gossip, vendor buy, item use / spell
cast with a ground target, and enough SMSG_UPDATE_OBJECT parsing to learn
object GUIDs, entries and positions.
"""

import hashlib
import hmac
import math
import os
import queue
import socket
import struct
import threading
import time
import zlib

BUILD = 12340

# Opcodes (from src/server/game/Server/Protocol/Opcodes.h)
CMSG_CHAR_CREATE = 0x036
CMSG_CHAR_DELETE = 0x038
SMSG_CHAR_DELETE = 0x03C
CMSG_AUTOBANK_ITEM = 0x283
CMSG_DESTROYITEM = 0x111
CMSG_CHAR_ENUM = 0x037
SMSG_CHAR_CREATE = 0x03A
SMSG_CHAR_ENUM = 0x03B
CMSG_PLAYER_LOGIN = 0x03D
SMSG_NEW_WORLD = 0x03E
SMSG_TRANSFER_PENDING = 0x03F
SMSG_TRANSFER_ABORTED = 0x040
CMSG_LOGOUT_REQUEST = 0x04B
SMSG_LOGOUT_COMPLETE = 0x04D
CMSG_MESSAGECHAT = 0x095
SMSG_MESSAGECHAT = 0x096
SMSG_UPDATE_OBJECT = 0x0A9
SMSG_DESTROY_OBJECT = 0x0AA
CMSG_USE_ITEM = 0x0AB
CMSG_GAMEOBJ_USE = 0x0B1
CMSG_SET_SELECTION = 0x13D
MSG_MOVE_TELEPORT_ACK = 0x0C7
MSG_MOVE_WORLDPORT_ACK = 0x0DC
MSG_MOVE_HEARTBEAT = 0x0EE
SMSG_STANDSTATE_UPDATE = 0x29D
CMSG_GET_MAIL_LIST = 0x23A
CMSG_GET_MIRRORIMAGE_DATA = 0x401
SMSG_MIRRORIMAGE_DATA = 0x402
SMSG_MAIL_LIST_RESULT = 0x23B
SMSG_SHOW_BANK = 0x1B8
SMSG_LOGIN_SETTIMESPEED = 0x042
SMSG_WEATHER = 0x2F4
SMSG_PLAY_MUSIC = 0x277
CMSG_BUY_BANK_SLOT = 0x1B9
SMSG_BUY_BANK_SLOT_RESULT = 0x1BA
CMSG_CAST_SPELL = 0x12E
SMSG_CAST_FAILED = 0x130
SMSG_SPELL_GO = 0x132
CMSG_INSTANCE_LOCK_RESPONSE = 0x13F
SMSG_INSTANCE_LOCK_WARNING_QUERY = 0x147
SMSG_ITEM_PUSH_RESULT = 0x166
CMSG_GOSSIP_HELLO = 0x17B
CMSG_GOSSIP_SELECT_OPTION = 0x17C
SMSG_GOSSIP_MESSAGE = 0x17D
SMSG_QUESTGIVER_QUEST_DETAILS = 0x188
CMSG_QUESTGIVER_ACCEPT_QUEST = 0x189
CMSG_QUESTGIVER_COMPLETE_QUEST = 0x18A
CMSG_QUESTGIVER_CHOOSE_REWARD = 0x18E
SMSG_QUESTGIVER_QUEST_COMPLETE = 0x191
SMSG_QUESTUPDATE_COMPLETE = 0x198
SMSG_GOSSIP_COMPLETE = 0x17E
CMSG_LIST_INVENTORY = 0x19E
SMSG_LIST_INVENTORY = 0x19F
CMSG_BUY_ITEM = 0x1A2
SMSG_BUY_ITEM = 0x1A4
SMSG_BUY_FAILED = 0x1A5
SMSG_NOTIFICATION = 0x1CB
CMSG_PING = 0x1DC
SMSG_AUTH_CHALLENGE = 0x1EC
CMSG_AUTH_SESSION = 0x1ED
SMSG_AUTH_RESPONSE = 0x1EE
SMSG_COMPRESSED_UPDATE_OBJECT = 0x1F6
SMSG_MONSTER_MOVE = 0x0DD
SMSG_LOGIN_VERIFY_WORLD = 0x236
SMSG_TIME_SYNC_REQ = 0x390
CMSG_TIME_SYNC_RESP = 0x391

CHAT_MSG_SYSTEM = 0x00
CHAT_MSG_SAY = 0x01
CHAT_MSG_WHISPER = 0x07
LANG_ADDON = 0xFFFFFFFF
LANG_COMMON = 7
LANG_ORCISH = 1

TYPEID_ITEM = 1
TYPEID_CONTAINER = 2
TYPEID_UNIT = 3
TYPEID_PLAYER = 4
TYPEID_GAMEOBJECT = 5

INVENTORY_SLOT_BAG_0 = 255
TARGET_FLAG_DEST_LOCATION = 0x40

SRP_N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
SRP_G = 7

SERVER_ENCRYPTION_KEY = bytes([0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA,
                               0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57])
SERVER_DECRYPTION_KEY = bytes([0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5,
                               0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE])


def sha1(*parts):
    h = hashlib.sha1()
    for p in parts:
        h.update(p)
    return h.digest()


def int_le(b):
    return int.from_bytes(b, "little")


def le_bytes(n, size=32):
    return n.to_bytes(size, "little")


def srp6_verifier(username, password, salt):
    """Registration helper: returns the 32-byte little-endian verifier for acore_auth.account."""
    x = int_le(sha1(salt, sha1((username.upper() + ":" + password.upper()).encode())))
    return le_bytes(pow(SRP_G, x, SRP_N))


def sha1_interleave(S):
    buf0 = S[0::2]
    buf1 = S[1::2]
    p = 0
    while p < len(S) and S[p] == 0:
        p += 1
    if p & 1:
        p += 1
    p //= 2
    h0 = sha1(buf0[p:])
    h1 = sha1(buf1[p:])
    out = bytearray(40)
    out[0::2] = h0
    out[1::2] = h1
    return bytes(out)


class RC4:
    def __init__(self, key):
        s = list(range(256))
        j = 0
        for i in range(256):
            j = (j + s[i] + key[i % len(key)]) & 0xFF
            s[i], s[j] = s[j], s[i]
        self.s = s
        self.i = 0
        self.j = 0

    def process(self, data):
        s = self.s
        i, j = self.i, self.j
        out = bytearray(len(data))
        for n, c in enumerate(data):
            i = (i + 1) & 0xFF
            j = (j + s[i]) & 0xFF
            s[i], s[j] = s[j], s[i]
            out[n] = c ^ s[(s[i] + s[j]) & 0xFF]
        self.i, self.j = i, j
        return bytes(out)


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def left(self):
        return len(self.d) - self.p

    def take(self, n):
        if self.p + n > len(self.d):
            raise EOFError("read past end (%d + %d > %d)" % (self.p, n, len(self.d)))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]

    def cstr(self):
        end = self.d.index(b"\x00", self.p)
        s = self.d[self.p:end].decode("utf-8", "replace")
        self.p = end + 1
        return s

    def packed_guid(self):
        mask = self.u8()
        guid = 0
        for i in range(8):
            if mask & (1 << i):
                guid |= self.u8() << (i * 8)
        return guid


def pack_guid(guid):
    mask = 0
    out = bytearray()
    for i in range(8):
        b = (guid >> (i * 8)) & 0xFF
        if b:
            mask |= 1 << i
            out.append(b)
    return bytes([mask]) + bytes(out)


class AuthError(Exception):
    pass


def auth_login(host, port, username, password):
    """Runs the SRP6 logon exchange and returns (session_key, realms)."""
    username = username.upper()
    password = password.upper()
    s = socket.create_connection((host, port), timeout=15)
    try:
        name = username.encode()
        body = (b"WoW\x00" + bytes([3, 3, 5]) + struct.pack("<H", BUILD) + b"68x\x00" + b"niW\x00"
                + b"SUne" + struct.pack("<I", 0) + bytes([127, 0, 0, 1]) + bytes([len(name)]) + name)
        s.sendall(bytes([0x00, 0x08]) + struct.pack("<H", len(body)) + body)

        hdr = recv_exact(s, 3)
        if hdr[2] != 0:
            raise AuthError("logon challenge failed, result=0x%02x" % hdr[2])
        B = recv_exact(s, 32)
        glen = recv_exact(s, 1)[0]
        g = int_le(recv_exact(s, glen))
        nlen = recv_exact(s, 1)[0]
        N = int_le(recv_exact(s, nlen))
        salt = recv_exact(s, 32)
        recv_exact(s, 16)  # version challenge
        sec_flags = recv_exact(s, 1)[0]
        if sec_flags:
            raise AuthError("account requires PIN/matrix/token (flags %d)" % sec_flags)

        a = int.from_bytes(os.urandom(19), "big")
        A = le_bytes(pow(g, a, N))
        u = int_le(sha1(A, B))
        x = int_le(sha1(salt, sha1((username + ":" + password).encode())))
        k = 3
        Bn = int_le(B)
        S = pow((Bn - k * pow(g, x, N)) % N, a + u * x, N)
        K = sha1_interleave(le_bytes(S))
        ng = bytes(p ^ q for p, q in zip(sha1(le_bytes(N)), sha1(bytes([g]))))
        M1 = sha1(ng, sha1(username.encode()), salt, A, B, K)

        s.sendall(bytes([0x01]) + A + M1 + bytes(20) + bytes([0, 0]))
        resp = recv_exact(s, 2)
        if resp[1] != 0:
            raise AuthError("logon proof rejected, error=0x%02x (bad password?)" % resp[1])
        recv_exact(s, 20 + 4 + 4 + 2)

        s.sendall(bytes([0x10]) + struct.pack("<I", 0))
        hdr = recv_exact(s, 3)
        size = struct.unpack("<H", hdr[1:3])[0]
        r = Reader(recv_exact(s, size))
        r.u32()
        count = r.u16()
        realms = []
        for _ in range(count):
            rtype = r.u8()
            locked = r.u8()
            flags = r.u8()
            rname = r.cstr()
            addr = r.cstr()
            r.f32()
            r.u8()
            r.u8()
            rid = r.u8()
            if flags & 0x04:
                r.take(5)
            realms.append({"name": rname, "address": addr, "id": rid, "type": rtype, "locked": locked})
        return K, realms
    finally:
        s.close()


def recv_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed")
        buf += chunk
    return bytes(buf)


class WorldObject:
    __slots__ = ("guid", "type_id", "entry", "x", "y", "z", "o", "fields", "rotation")

    def __init__(self, guid, type_id):
        self.guid = guid
        self.type_id = type_id
        self.entry = 0
        self.x = self.y = self.z = self.o = 0.0
        self.fields = {}
        self.rotation = None  # game objects: (x, y, z, w) from the packed rotation

    def scale(self):
        """OBJECT_FIELD_SCALE_X."""
        return struct.unpack("<f", struct.pack("<I", self.fields.get(4, 0)))[0]

    def yaw_pitch_roll(self):
        """A game object's rotation as the angles the server builds it from (Z, then Y, then X)."""
        x, y, z, w = self.rotation
        yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        pitch = math.asin(max(-1.0, min(1.0, 2 * (w * y - x * z))))
        roll = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        return yaw, pitch, roll

    def __repr__(self):
        return "<obj guid=0x%x type=%d entry=%d pos=(%.2f, %.2f, %.2f)>" % (
            self.guid, self.type_id, self.entry, self.x, self.y, self.z)


class WorldClient:
    def __init__(self, host, port, account, session_key, realm_id=1, log=None):
        self.host = host
        self.port = port
        self.account = account.upper()
        self.K = session_key
        self.realm_id = realm_id
        self.log = log or (lambda msg: None)
        self.sock = None
        self._rotation = None
        self.enc = None
        self.dec = None
        self.send_lock = threading.Lock()
        self.packets = queue.Queue()
        self.objects = {}
        self.objects_lock = threading.Lock()
        self.system_messages = []
        self.addon_messages = []
        self.monster_moves = {}        # guid -> how many moves the server sent for it
        self.stand_state = 0
        self.mailbox_opened = 0
        self.bank_banker = None        # the banker of the last SMSG_SHOW_BANK
        self.weather = None            # (state, grade) of the last SMSG_WEATHER
        self.clock = None              # (hour, minute) of the last SMSG_LOGIN_SETTIMESPEED
        self.music = []                # sound ids from SMSG_PLAY_MUSIC
        self.quests_done = []          # quest ids whose objectives completed (SMSG_QUESTUPDATE_COMPLETE)
        self.quests_rewarded = []      # quest ids turned in (SMSG_QUESTGIVER_QUEST_COMPLETE)
        self.quest_offered = None      # the quest a giver offered next (SMSG_QUESTGIVER_QUEST_DETAILS)
        self.bank_slot_results = []    # SMSG_BUY_BANK_SLOT_RESULT codes
        self.mirror_images = {}
        self.msg_lock = threading.Lock()
        self.player_guid = 0
        self.map_id = None
        self.pos = (0.0, 0.0, 0.0, 0.0)
        self.move_counter = 0
        self.alive = False
        self.start_ms = int(time.time() * 1000)
        self.items_pushed = []
        self.last_gossip = None
        self.last_vendor = None
        self.world_changes = []
        self.errors = []

    # ------------------------------------------------------------------ io
    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=30)
        size, opcode, payload = self._read_plain()
        if opcode != SMSG_AUTH_CHALLENGE:
            raise AuthError("expected SMSG_AUTH_CHALLENGE, got 0x%x" % opcode)
        server_seed = payload[4:8]
        client_seed = os.urandom(4)
        digest = sha1(self.account.encode(), b"\x00\x00\x00\x00", client_seed, server_seed, self.K)
        addon_plain = struct.pack("<I", 0) + struct.pack("<I", int(time.time()))
        addon = struct.pack("<I", len(addon_plain)) + zlib.compress(addon_plain)
        body = (struct.pack("<II", BUILD, 0) + self.account.encode() + b"\x00" + struct.pack("<I", 0)
                + client_seed + struct.pack("<IIIQ", 0, 0, self.realm_id, 0) + digest + addon)
        self._send_raw(CMSG_AUTH_SESSION, body, encrypt=False)
        self.enc = RC4(hmac.new(SERVER_DECRYPTION_KEY, self.K, hashlib.sha1).digest())
        self.dec = RC4(hmac.new(SERVER_ENCRYPTION_KEY, self.K, hashlib.sha1).digest())
        self.enc.process(bytes(1024))
        self.dec.process(bytes(1024))
        self.alive = True
        threading.Thread(target=self._reader, daemon=True).start()
        op, data = self.wait_for(SMSG_AUTH_RESPONSE, timeout=15)
        if data[0] != 0x0C:
            raise AuthError("world auth failed, code=0x%02x" % data[0])

    def _read_plain(self):
        hdr = recv_exact(self.sock, 4)
        size = struct.unpack(">H", hdr[:2])[0]
        opcode = struct.unpack("<H", hdr[2:])[0]
        return size, opcode, recv_exact(self.sock, size - 2)

    def _send_raw(self, opcode, body, encrypt=True):
        header = struct.pack(">H", len(body) + 4) + struct.pack("<I", opcode)
        with self.send_lock:
            if encrypt:
                header = self.enc.process(header)
            self.sock.sendall(header + body)

    def send(self, opcode, body=b""):
        self._send_raw(opcode, body, encrypt=True)

    def send_many(self, packets):
        """Several packets in one write, so the server takes them in the same go."""
        data = b""
        with self.send_lock:
            for opcode, body in packets:
                header = struct.pack(">H", len(body) + 4) + struct.pack("<I", opcode)
                data += self.enc.process(header) + body
            self.sock.sendall(data)

    def _reader(self):
        try:
            self.sock.settimeout(None)
            while self.alive:
                first = self.dec.process(recv_exact(self.sock, 1))
                if first[0] & 0x80:
                    rest = self.dec.process(recv_exact(self.sock, 4))
                    size = ((first[0] & 0x7F) << 16) | (rest[0] << 8) | rest[1]
                    opcode = struct.unpack("<H", rest[2:4])[0]
                else:
                    rest = self.dec.process(recv_exact(self.sock, 3))
                    size = (first[0] << 8) | rest[0]
                    opcode = struct.unpack("<H", rest[1:3])[0]
                payload = recv_exact(self.sock, size - 2)
                try:
                    self._dispatch(opcode, payload)
                except Exception as exc:  # parsing problems should not kill the reader
                    self.errors.append("opcode 0x%x: %r" % (opcode, exc))
                self.packets.put((opcode, payload))
        except Exception as exc:
            if self.alive:
                self.errors.append("reader stopped: %r" % exc)
            self.alive = False
            self.packets.put((None, b""))

    # ------------------------------------------------------------ dispatch
    def _dispatch(self, opcode, data):
        if opcode == SMSG_TIME_SYNC_REQ:
            counter = struct.unpack("<I", data[:4])[0]
            self.send(CMSG_TIME_SYNC_RESP, struct.pack("<II", counter, self.now_ms()))
        elif opcode == SMSG_MESSAGECHAT:
            self._on_chat(data)
        elif opcode == SMSG_MONSTER_MOVE:
            self._on_monster_move(data)
        elif opcode == SMSG_STANDSTATE_UPDATE:
            self.stand_state = data[0]
        elif opcode == SMSG_MAIL_LIST_RESULT:
            self.mailbox_opened += 1
        elif opcode == SMSG_QUESTUPDATE_COMPLETE:
            self.quests_done.append(struct.unpack_from("<I", data)[0])
        elif opcode == SMSG_QUESTGIVER_QUEST_COMPLETE:
            self.quests_rewarded.append(struct.unpack_from("<I", data)[0])
        elif opcode == SMSG_QUESTGIVER_QUEST_DETAILS:
            self.quest_offered = struct.unpack_from("<QQI", data)[2]
        elif opcode == SMSG_WEATHER:
            self.weather = struct.unpack_from("<If", data)
        elif opcode == SMSG_LOGIN_SETTIMESPEED:
            packed = struct.unpack_from("<I", data)[0]
            self.clock = ((packed >> 6) & 0x1F, packed & 0x3F)
        elif opcode == SMSG_PLAY_MUSIC:
            self.music.append(struct.unpack_from("<I", data)[0])
        elif opcode == SMSG_SHOW_BANK:
            self.bank_banker = struct.unpack_from("<Q", data)[0]
        elif opcode == SMSG_BUY_BANK_SLOT_RESULT:
            self.bank_slot_results.append(struct.unpack_from("<I", data)[0])
        elif opcode == SMSG_MIRRORIMAGE_DATA:
            r = Reader(data)
            guid = r.u64()
            look = dict(display=r.u32(), race=r.u8(), gender=r.u8(), cls=r.u8())
            r.take(5)
            r.u32()
            look["items"] = [r.u32() for _ in range(11)]  # head, shoulders, shirt, chest, waist, legs, feet, wrists, hands, back, tabard
            self.mirror_images[guid] = look
        elif opcode == SMSG_NOTIFICATION:
            text = Reader(data).cstr()
            self._add_message("[notify] " + text)
        elif opcode == SMSG_UPDATE_OBJECT:
            self._on_update(data)
        elif opcode == SMSG_COMPRESSED_UPDATE_OBJECT:
            size = struct.unpack("<I", data[:4])[0]
            self._on_update(zlib.decompress(data[4:], bufsize=size))
        elif opcode == SMSG_DESTROY_OBJECT:
            guid = struct.unpack("<Q", data[:8])[0]
            with self.objects_lock:
                self.objects.pop(guid, None)
        elif opcode == SMSG_LOGIN_VERIFY_WORLD:
            r = Reader(data)
            self.map_id = r.u32()
            self.pos = (r.f32(), r.f32(), r.f32(), r.f32())
        elif opcode == SMSG_NEW_WORLD:
            r = Reader(data)
            self.map_id = r.u32()
            self.pos = (r.f32(), r.f32(), r.f32(), r.f32())
            self.world_changes.append((self.map_id, self.pos))
            with self.objects_lock:
                self.objects = {g: o for g, o in self.objects.items() if g == self.player_guid or o.type_id in (TYPEID_ITEM, TYPEID_CONTAINER)}
            self.send(MSG_MOVE_WORLDPORT_ACK)
        elif opcode == MSG_MOVE_TELEPORT_ACK:
            r = Reader(data)
            guid = r.packed_guid()
            counter = r.u32()
            r.u32(); r.u16(); r.u32()
            self.pos = (r.f32(), r.f32(), r.f32(), r.f32())
            self.send(MSG_MOVE_TELEPORT_ACK, pack_guid(guid) + struct.pack("<II", counter, self.now_ms()))
        elif opcode == SMSG_INSTANCE_LOCK_WARNING_QUERY:
            self.send(CMSG_INSTANCE_LOCK_RESPONSE, b"\x01")
        elif opcode == SMSG_ITEM_PUSH_RESULT:
            r = Reader(data)
            r.u64(); r.u32(); r.u32(); r.u32()
            bag = r.u8()
            slot = r.i32()
            entry = r.u32()
            self.items_pushed.append({"bag": bag, "slot": slot, "entry": entry})
        elif opcode == SMSG_GOSSIP_MESSAGE:
            self.last_gossip = self._parse_gossip(data)
        elif opcode == SMSG_LIST_INVENTORY:
            self.last_vendor = self._parse_vendor(data)

    def _add_message(self, text):
        with self.msg_lock:
            self.system_messages.append(text)
        self.log("  << " + text)

    def _on_chat(self, data):
        r = Reader(data)
        ctype = r.u8()
        lang = r.i32()
        r.u64()
        r.u32()
        if ctype in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x29, 0x2A, 0x2F):
            r.take(r.u32())
            recv = r.u64()
            if recv and (recv >> 48) not in (0x0000, 0xF140):
                r.take(r.u32())
        elif ctype == 0x08:
            r.take(r.u32())
            r.u64()
        elif ctype == 0x11:
            r.cstr()
            r.u64()
        else:
            r.u64()
        length = r.u32()
        text = r.take(length).rstrip(b"\x00").decode("utf-8", "replace")
        if ctype == CHAT_MSG_SYSTEM:
            self._add_message(text)
        elif lang == -1:  # LANG_ADDON: what a client addon would get as CHAT_MSG_ADDON
            self.addon_messages.append(text)

    def _on_monster_move(self, data):
        """A creature gliding somewhere: its object takes the destination (and final facing) at
        once, which is where it will be."""
        r = Reader(data)
        guid = r.packed_guid()
        self.monster_moves[guid] = self.monster_moves.get(guid, 0) + 1
        r.u8()
        r.take(12)  # where it starts
        r.u32()     # spline id
        kind = r.u8()
        facing = None
        if kind == 1:  # stop
            return
        if kind == 2:
            r.take(12)
        elif kind == 3:
            r.take(8)
        elif kind == 4:
            facing = struct.unpack("<f", r.take(4))[0]
        flags = r.u32()
        if flags & 0x00200000:  # animation
            r.take(5)
        r.u32()  # duration
        if flags & 0x00000800:  # parabolic
            r.take(8)
        count = r.u32()
        if count < 1:
            return
        if flags & (0x00002000 | 0x00040000):  # flying or catmull-rom: every point
            points = [struct.unpack("<3f", r.take(12)) for _ in range(count)]
            destination = points[-1]
        else:
            destination = struct.unpack("<3f", r.take(12))
        with self.objects_lock:
            obj = self.objects.get(guid)
            if obj:
                obj.x, obj.y, obj.z = destination
                if facing is not None:
                    obj.o = facing

    def _parse_gossip(self, data):
        r = Reader(data)
        guid = r.u64()
        menu_id = r.u32()
        r.u32()
        items = []
        for _ in range(r.u32()):
            idx = r.u32()
            icon = r.u8()
            coded = r.u8()
            r.u32()
            text = r.cstr()
            r.cstr()
            items.append({"index": idx, "icon": icon, "coded": bool(coded), "text": text})
        quests = []
        if r.left() >= 4:
            for _ in range(r.u32()):
                quest_id = r.u32()
                r.u32(); r.i32(); r.u32(); r.u8()
                quests.append({"id": quest_id, "title": r.cstr()})
        return {"guid": guid, "menu_id": menu_id, "items": items, "quests": quests}

    def _parse_vendor(self, data):
        r = Reader(data)
        guid = r.u64()
        count = r.u8()
        items = []
        for _ in range(count):
            slot = r.u32()
            entry = r.u32()
            r.u32()
            r.i32()
            price = r.u32()
            r.u32()
            r.u32()
            r.u32()
            items.append({"slot": slot, "entry": entry, "price": price})
        return {"guid": guid, "items": items}

    # ---------------------------------------------------- update objects
    def _on_update(self, data):
        r = Reader(data)
        count = r.u32()
        for _ in range(count):
            utype = r.u8()
            if utype == 0:  # VALUES
                guid = r.packed_guid()
                fields = self._read_values(r)
                with self.objects_lock:
                    obj = self.objects.get(guid)
                    if obj:
                        obj.fields.update(fields)
                        if 3 in fields:
                            obj.entry = fields[3]
            elif utype == 1:  # MOVEMENT
                guid = r.packed_guid()
                pos = self._read_movement(r)
                with self.objects_lock:
                    obj = self.objects.get(guid)
                    if obj and pos:
                        obj.x, obj.y, obj.z, obj.o = pos
            elif utype in (2, 3):  # CREATE_OBJECT / CREATE_OBJECT2
                guid = r.packed_guid()
                type_id = r.u8()
                self._rotation = None
                pos, flags = self._read_movement(r, want_flags=True)
                fields = self._read_values(r)
                obj = WorldObject(guid, type_id)
                obj.fields = fields
                obj.rotation = self._rotation
                obj.entry = fields.get(3, 0)
                if pos:
                    obj.x, obj.y, obj.z, obj.o = pos
                with self.objects_lock:
                    self.objects[guid] = obj
                if type_id == TYPEID_PLAYER and flags & 0x01:  # UPDATEFLAG_SELF
                    self.player_guid = guid
                    if pos:
                        self.pos = pos
            elif utype in (4, 5):  # OUT_OF_RANGE / NEAR
                n = r.u32()
                gone = [r.packed_guid() for _ in range(n)]
                if utype == 4:
                    with self.objects_lock:
                        for g in gone:
                            if g != self.player_guid:
                                self.objects.pop(g, None)
            else:
                raise ValueError("unknown update type %d" % utype)

    def _read_values(self, r):
        blocks = r.u8()
        masks = [r.u32() for _ in range(blocks)]
        fields = {}
        for bi, mask in enumerate(masks):
            for bit in range(32):
                if mask & (1 << bit):
                    fields[bi * 32 + bit] = r.u32()
        return fields

    def _read_movement(self, r, want_flags=False):
        flags = r.u16()
        pos = None
        if flags & 0x20:  # LIVING
            mflags = r.u32()
            mflags2 = r.u16()
            r.u32()
            pos = (r.f32(), r.f32(), r.f32(), r.f32())
            if mflags & 0x200:  # ON_TRANSPORT
                r.packed_guid()
                r.take(16)
                r.u32()
                r.u8()
                if mflags2 & 0x400:
                    r.u32()
            if (mflags & (0x200000 | 0x2000000)) or (mflags2 & 0x20):
                r.f32()
            r.u32()
            if mflags & 0x1000:
                r.take(16)
            if mflags & 0x4000000:
                r.f32()
            r.take(9 * 4)
            if mflags & 0x8000000:  # SPLINE_ENABLED
                sflags = r.u32()
                if sflags & 0x20000:
                    r.f32()
                elif sflags & 0x10000:
                    r.u64()
                elif sflags & 0x8000:
                    r.take(12)
                r.take(4 * 3)  # time passed, duration, id
                r.take(4 * 3)  # duration mods, vertical accel
                r.u32()        # effect start time
                nodes = r.u32()
                r.take(nodes * 12)
                r.u8()
                r.take(12)
        elif flags & 0x100:  # POSITION
            r.packed_guid()
            x, y, z = r.f32(), r.f32(), r.f32()
            r.take(12)
            o = r.f32()
            r.f32()
            pos = (x, y, z, o)
        elif flags & 0x40:  # STATIONARY_POSITION
            pos = (r.f32(), r.f32(), r.f32(), r.f32())
        if flags & 0x08:
            r.u32()
        if flags & 0x10:
            r.u32()
        if flags & 0x04:
            r.packed_guid()
        if flags & 0x02:
            r.u32()
        if flags & 0x80:
            r.u32()
            r.f32()
        if flags & 0x200:  # ROTATION: packed quaternion (x 22 bits, y and z 21 bits, w positive)
            packed = r.u64()
            def signed(value, bits):
                return value - (1 << bits) if value & (1 << (bits - 1)) else value
            qx = signed(packed >> 42, 22) / float(1 << 21)
            qy = signed((packed >> 21) & 0x1FFFFF, 21) / float(1 << 20)
            qz = signed(packed & 0x1FFFFF, 21) / float(1 << 20)
            self._rotation = (qx, qy, qz, math.sqrt(max(0.0, 1.0 - qx * qx - qy * qy - qz * qz)))
        if want_flags:
            return pos, flags
        return pos

    # --------------------------------------------------------- helpers
    def now_ms(self):
        return (int(time.time() * 1000) - self.start_ms) & 0xFFFFFFFF

    def wait_for(self, opcode, timeout=10, predicate=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                op, data = self.packets.get(timeout=max(0.05, deadline - time.time()))
            except queue.Empty:
                break
            if op is None:
                raise ConnectionError("world connection closed: %s" % self.errors[-1:] )
            if op == opcode and (predicate is None or predicate(data)):
                return op, data
        raise TimeoutError("timed out waiting for opcode 0x%x" % opcode)

    def drain(self):
        while True:
            try:
                self.packets.get_nowait()
            except queue.Empty:
                return

    def pump(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                op, _ = self.packets.get(timeout=max(0.05, deadline - time.time()))
                if op is None:
                    raise ConnectionError("world connection closed: %s" % self.errors[-1:])
            except queue.Empty:
                pass

    def messages_since(self, mark):
        with self.msg_lock:
            return self.system_messages[mark:]

    def message_mark(self):
        with self.msg_lock:
            return len(self.system_messages)

    # -------------------------------------------------------- characters
    def enum_chars(self):
        self.send(CMSG_CHAR_ENUM)
        _, data = self.wait_for(SMSG_CHAR_ENUM)
        r = Reader(data)
        chars = []
        for _ in range(r.u8()):
            guid = r.u64()
            name = r.cstr()
            race, cls = r.u8(), r.u8()
            r.take(6)
            level = r.u8()
            r.u32()
            mapid = r.u32()
            r.take(12)
            r.take(4 * 3)
            r.u8()
            r.take(12)
            r.take(23 * 9)
            chars.append({"guid": guid, "name": name, "race": race, "class": cls, "level": level, "map": mapid})
        return chars

    def create_char(self, name, race=1, cls=1, gender=0):
        body = name.encode() + b"\x00" + bytes([race, cls, gender, 0, 0, 0, 0, 0, 0])
        self.send(CMSG_CHAR_CREATE, body)
        _, data = self.wait_for(SMSG_CHAR_CREATE)
        return data[0]

    def login(self, guid):
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, timeout=30)
        self.player_guid = guid
        self.pump(2.0)

    def logout(self):
        self.send(CMSG_LOGOUT_REQUEST)
        try:
            self.wait_for(SMSG_LOGOUT_COMPLETE, timeout=30)
        finally:
            self.close()

    def logout_to_characters(self):
        """Back to the character list, still connected (20 seconds outside an inn or city)."""
        self.send(CMSG_LOGOUT_REQUEST)
        self.wait_for(SMSG_LOGOUT_COMPLETE, timeout=30)
        self.player_guid = 0

    def delete_char(self, guid):
        self.send(CMSG_CHAR_DELETE, struct.pack("<Q", guid))
        _, data = self.wait_for(SMSG_CHAR_DELETE)
        return data[0]

    def close(self):
        self.alive = False
        try:
            self.sock.close()
        except Exception:
            pass

    # ----------------------------------------------------------- actions
    def say(self, text, lang=LANG_COMMON, wait=1.5):
        mark = self.message_mark()
        body = struct.pack("<II", CHAT_MSG_SAY, lang) + text.encode() + b"\x00"
        self.send(CMSG_MESSAGECHAT, body)
        self.pump(wait)
        return self.messages_since(mark)

    def command(self, text, lang=LANG_COMMON, wait=1.5):
        self.log("  >> " + text)
        return self.say(text, lang, wait)

    def addon_command(self, text, to, echo=1, wait=0.0):
        """A command over AzerothCore's addon command channel, as the Player Housing addon sends
        quick ones: an addon whisper to yourself, prefix AzerothCore, "i", a 4-character echo,
        then the command without its dot. The answers come back as addon messages."""
        message = "AzerothCore\ti%04d%s" % (echo % 10000, text)
        body = struct.pack("<II", CHAT_MSG_WHISPER, LANG_ADDON) + to.encode() + b"\x00" + message.encode() + b"\x00"
        self.send(CMSG_MESSAGECHAT, body)
        if wait:
            self.pump(wait)

    def find_objects(self, entry=None, type_id=None):
        with self.objects_lock:
            objs = list(self.objects.values())
        return [o for o in objs if (entry is None or o.entry == entry) and (type_id is None or o.type_id == type_id)]

    def nearest(self, entry, type_id=None):
        px, py, pz, _ = self.pos
        objs = self.find_objects(entry, type_id)
        if not objs:
            return None
        return min(objs, key=lambda o: (o.x - px) ** 2 + (o.y - py) ** 2 + (o.z - pz) ** 2)

    def move_to(self, x, y, z, o=None):
        """Reports a new position via a heartbeat (server trusts client movement)."""
        if o is None:
            o = self.pos[3]
        body = (pack_guid(self.player_guid) + struct.pack("<IHI", 0, 0, self.now_ms())
                + struct.pack("<ffff", x, y, z, o) + struct.pack("<I", 0))
        self.send(MSG_MOVE_HEARTBEAT, body)
        self.pos = (x, y, z, o)
        self.pump(0.3)

    def gossip_hello(self, guid, wait=2.0):
        mark = self.message_mark()
        self.last_gossip = None
        self.send(CMSG_GOSSIP_HELLO, struct.pack("<Q", guid))
        self.pump(wait)
        return self.last_gossip, self.messages_since(mark)

    def gossip_select(self, text_prefix, code=None, wait=2.0):
        menu = self.last_gossip
        if not menu:
            raise RuntimeError("no gossip menu open")
        for item in menu["items"]:
            if item["text"].startswith(text_prefix):
                break
        else:
            raise RuntimeError("gossip option %r not in %r" % (text_prefix, [i["text"] for i in menu["items"]]))
        mark = self.message_mark()
        self.last_gossip = None
        body = struct.pack("<QII", menu["guid"], menu["menu_id"], item["index"])
        if item["coded"]:
            body += (code or "").encode() + b"\x00"
        self.log("  >> gossip: %s%s" % (item["text"], " [%s]" % code if code else ""))
        self.send(CMSG_GOSSIP_SELECT_OPTION, body)
        self.pump(wait)
        return self.last_gossip, self.messages_since(mark)

    def list_vendor(self, guid, wait=2.0):
        self.last_vendor = None
        self.send(CMSG_LIST_INVENTORY, struct.pack("<Q", guid))
        self.pump(wait)
        return self.last_vendor

    def buy(self, vendor_guid, item_entry, vendor_slot, count=1, wait=2.0):
        mark = self.message_mark()
        before = len(self.items_pushed)
        self.send(CMSG_BUY_ITEM, struct.pack("<QIIIB", vendor_guid, item_entry, vendor_slot, count, 0))
        self.pump(wait)
        return self.items_pushed[before:], self.messages_since(mark)

    def item_guid_for_entry(self, entry):
        for o in self.find_objects(entry, TYPEID_ITEM):
            return o.guid
        return None

    def use_item_at(self, bag, slot, item_guid, spell_id, x, y, z, wait=2.5):
        mark = self.message_mark()
        targets = struct.pack("<I", TARGET_FLAG_DEST_LOCATION) + b"\x00" + struct.pack("<fff", x, y, z)
        body = struct.pack("<BBBIQIB", bag, slot, 1, spell_id, item_guid, 0, 0) + targets
        self.send(CMSG_USE_ITEM, body)
        self.pump(wait)
        return self.messages_since(mark)

    # -------------------------------------------------------------- bags
    # PLAYER_FIELD_INV_SLOT_HEAD: one 64-bit item guid per inventory slot; the backpack is
    # slots 23-38. ITEM_FIELD_STACK_COUNT is field 14 of an item.
    def backpack(self):
        with self.objects_lock:
            me = self.objects.get(self.player_guid)
            fields = dict(me.fields) if me else {}
            items = {g: o for g, o in self.objects.items() if o.type_id == TYPEID_ITEM}
        slots = {}
        for slot in range(23, 39):
            low, high = fields.get(324 + 2 * slot, 0), fields.get(325 + 2 * slot, 0)
            guid = low | (high << 32)
            if guid and guid in items:
                item = items[guid]
                slots[slot] = (guid, item.entry, item.fields.get(14, 1))
        return slots

    def bank_items(self):
        """The bank's own 28 slots (39-66), as backpack() gives the backpack's."""
        with self.objects_lock:
            me = self.objects.get(self.player_guid)
            fields = dict(me.fields) if me else {}
            items = {g: o for g, o in self.objects.items() if o.type_id == TYPEID_ITEM}
        slots = {}
        for slot in range(39, 67):
            low, high = fields.get(324 + 2 * slot, 0), fields.get(325 + 2 * slot, 0)
            guid = low | (high << 32)
            if guid and guid in items:
                slots[slot] = (guid, items[guid].entry, items[guid].fields.get(14, 1))
        return slots

    def count_item(self, entry):
        return sum(count for guid, e, count in self.backpack().values() if e == entry)

    def find_item(self, entry):
        for slot, (guid, e, count) in sorted(self.backpack().items()):
            if e == entry:
                return slot, guid
        return None, None

    def use_item(self, entry, spell_id, dest=None, wait=2.0):
        """Uses the first backpack item with this entry, at dest (x, y, z) when given."""
        slot, guid = self.find_item(entry)
        if guid is None:
            raise RuntimeError("item %d is not in the backpack: %r" % (entry, self.backpack()))
        mark = self.message_mark()
        self.last_gossip = None
        if dest:
            targets = struct.pack("<I", TARGET_FLAG_DEST_LOCATION) + b"\x00" + struct.pack("<fff", *dest)
        else:
            targets = struct.pack("<I", 0)
        self.send(CMSG_USE_ITEM, struct.pack("<BBBIQIB", INVENTORY_SLOT_BAG_0, slot, 1, spell_id, guid, 0, 0) + targets)
        self.pump(wait)
        return self.messages_since(mark)

    def destroy_item(self, entry, wait=1.0):
        """Destroys the first backpack stack with this entry, like dragging it out of the bags."""
        slot, guid = self.find_item(entry)
        if guid is None:
            return False
        count = self.backpack()[slot][2]
        self.send(CMSG_DESTROYITEM, struct.pack("<BBBBBB", INVENTORY_SLOT_BAG_0, slot, min(count, 255), 0, 0, 0))
        self.pump(wait)
        return True

    def buy_bank_slot(self, banker_guid, wait=1.0):
        """What the bank window's Purchase button sends. Returns the server's answer."""
        before = len(self.bank_slot_results)
        self.send(CMSG_BUY_BANK_SLOT, struct.pack("<Q", banker_guid))
        deadline = time.time() + wait + 2.0
        while len(self.bank_slot_results) == before and time.time() < deadline:
            self.pump(0.2)
        return self.bank_slot_results[-1] if len(self.bank_slot_results) > before else None

    def accept_quest(self, giver_guid, quest_id, wait=1.0):
        self.send(CMSG_QUESTGIVER_ACCEPT_QUEST, struct.pack("<QII", giver_guid, quest_id, 0))
        self.pump(wait)

    def turn_in_quest(self, giver_guid, quest_id, wait=1.5):
        """Complete, then take the reward (no choice)."""
        self.send(CMSG_QUESTGIVER_COMPLETE_QUEST, struct.pack("<QI", giver_guid, quest_id))
        self.pump(0.5)
        self.send(CMSG_QUESTGIVER_CHOOSE_REWARD, struct.pack("<QII", giver_guid, quest_id, 0))
        self.pump(wait)
        return quest_id in self.quests_rewarded

    def open_mailbox(self, guid, wait=1.5):
        """What the client does on right-clicking a mailbox; the server answers only if the
        mailbox is really there and in reach."""
        self.send(CMSG_GET_MAIL_LIST, struct.pack("<Q", guid))
        self.pump(wait)

    def mirror_image(self, guid, wait=1.5):
        """What the client asks for when a mirror image comes into view: how it's dressed."""
        self.mirror_images.pop(guid, None)
        self.send(CMSG_GET_MIRRORIMAGE_DATA, struct.pack("<Q", guid))
        self.pump(wait)
        return self.mirror_images.get(guid)

    def use_gameobject(self, guid, wait=2.0):
        mark = self.message_mark()
        self.last_gossip = None
        self.send(CMSG_GAMEOBJ_USE, struct.pack("<Q", guid))
        self.pump(wait)
        return self.last_gossip, self.messages_since(mark)

    def select(self, guid):
        self.send(CMSG_SET_SELECTION, struct.pack("<Q", guid))
        self.pump(0.3)

    def cast_at(self, spell_id, x, y, z, wait=2.5):
        mark = self.message_mark()
        targets = struct.pack("<I", TARGET_FLAG_DEST_LOCATION) + b"\x00" + struct.pack("<fff", x, y, z)
        self.send(CMSG_CAST_SPELL, struct.pack("<BIB", 1, spell_id, 0) + targets)
        self.pump(wait)
        return self.messages_since(mark)

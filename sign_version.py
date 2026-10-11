"""Sign the update manifest (VERSION) for Mem Reduct++.

Installed copies only accept an update when VERSION carries a valid ECDSA P-256 signature from
the key built into the app (APP_UPDATE_PUBKEY in src/app.h) and the downloaded release exe
matches the SHA-256 listed for it.

  python sign_version.py genkey  <private.pem>          create a key pair, print the C public key
  python sign_version.py pubkey  <private.pem>          print the C public key again
  python sign_version.py release <private.pem> <version> <release exe>
        rewrite VERSION: memreduct=<version>|<asset url>|<sha256>; keep other records; sign
  python sign_version.py verify  <private.pem|public.pem> verify VERSION as the app would

Keep the private key out of the repository. Losing it means installed copies can no longer be
updated through the update check (users would have to download a new build by hand).

Signed bytes: everything in VERSION before the last "signature=", with the CR of each CRLF removed
(git may check the file out with CRLF; GitHub serves the LF blob). Any other CR, any non-ASCII
character, or anything but whitespace after the signature makes the app reject the file. The
record is "signature=<128 hex chars: r || s>;". `verify` applies exactly the app's rules.
"""
import hashlib, pathlib, re, sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

ROOT = pathlib.Path(__file__).resolve().parent
VERSION = ROOT / "VERSION"
REPO_URL = "https://github.com/Hantu-Raya/memreduct"


def load_key(path):
    data = pathlib.Path(path).read_bytes()
    if b"PRIVATE" in data:
        return serialization.load_pem_private_key(data, None)
    return serialization.load_pem_public_key(data)


def c_pubkey(key):
    pub = key.public_key() if hasattr(key, "public_key") else key
    n = pub.public_numbers()
    raw = n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")
    rows = [", ".join(f"0x{b:02X}" for b in raw[i:i + 16]) for i in range(0, 64, 16)]
    return "#define APP_UPDATE_PUBKEY { \\\n\t" + ", \\\n\t".join(rows) + " \\\n}"


MAX_LENGTH = 0x10000  # the app rejects longer manifests


def split_signed(text):
    """Parse a signed manifest exactly as the app does (_r_update_verifymanifest).

    Returns (signed body bytes, raw 64-byte signature) or None if the app would reject the text.
    """
    if len(text) > MAX_LENGTH:
        return None
    i = text.rfind("signature=")
    if i < 0:
        return None
    m = re.fullmatch(r"([0-9a-fA-F]{128});?[\r\n \t]*", text[i + 10:])
    if not m:
        return None
    for j in range(i):
        if ord(text[j]) > 0x7F or (text[j] == "\r" and text[j + 1] != "\n"):
            return None
    return text[:i].replace("\r", "").encode("ascii"), bytes.fromhex(m.group(1))


def sign(key, body):
    """Append the signature record to an unsigned, LF-only ASCII body."""
    if "\r" in body or len(body) + 140 > MAX_LENGTH:
        raise ValueError("body must be LF-only and short")
    digest = hashlib.sha256(body.encode("ascii")).digest()
    der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    return body + "signature=" + (r.to_bytes(32, "big") + s.to_bytes(32, "big")).hex() + ";"


def verify(key, text):
    pub = key.public_key() if hasattr(key, "public_key") else key
    parsed = split_signed(text)
    if not parsed:
        return False
    body, raw = parsed
    der = utils.encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    try:
        pub.verify(der, hashlib.sha256(body).digest(), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
        return True
    except Exception:
        return False


def read_version():
    # bytes, not read_text: no newline translation, exactly what the app receives
    return VERSION.read_bytes().decode("ascii")


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "genkey":
        path = pathlib.Path(argv[2])
        if path.exists():
            sys.exit(f"{path} already exists; refusing to overwrite a signing key")
        key = ec.generate_private_key(ec.SECP256R1())
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        path.with_suffix(".pub.pem").write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        print(c_pubkey(key))
    elif cmd == "pubkey":
        print(c_pubkey(load_key(argv[2])))
    elif cmd == "release":
        key, version, exe = load_key(argv[2]), argv[3], pathlib.Path(argv[4])
        sha = hashlib.sha256(exe.read_bytes()).hexdigest()
        url = f"{REPO_URL}/releases/download/v{version}/memreduct-plusplus-{version}-x64.exe"
        text = read_version()
        parsed = split_signed(text)
        if parsed:
            body = parsed[0].decode("ascii")
        elif "signature=" not in text:
            body = text.replace("\r", "")
        else:
            sys.exit("VERSION has a malformed signature record; fix it by hand first")
        records = [r.strip() for r in body.split(";") if r.strip()]
        if not any(r.startswith("memreduct=") for r in records):
            records.insert(0, "memreduct=")
        records = [f"memreduct={version}|{url}|{sha}" if r.startswith("memreduct=") else r for r in records]
        text = sign(key, "".join(r + ";\n" for r in records))
        if not verify(key, text):
            sys.exit("internal error: the signed VERSION does not verify")
        VERSION.write_bytes(text.encode("ascii"))
        print(f"VERSION signed: memreduct={version} sha256={sha}")
    elif cmd == "verify":
        try:
            ok = verify(load_key(argv[2]), read_version())
        except UnicodeDecodeError:
            ok = False
        print("VERSION signature valid" if ok else "VERSION signature INVALID")
        sys.exit(0 if ok else 1)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

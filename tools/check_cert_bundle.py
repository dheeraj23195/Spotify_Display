# Checks that data/cert/x509_crt_bundle.bin can verify the servers the display talks to.
# Emulates the ESP32 check: find the bundle entry whose subject == issuer of the last
# certificate the server sends, then verify that certificate's signature.
# Usage: python3 tools/check_cert_bundle.py [bundle.bin]   (needs: pip install cryptography)
import struct, sys
from cryptography import x509
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import padding, ec

data = open(sys.argv[1] if len(sys.argv) > 1 else 'data/cert/x509_crt_bundle.bin', 'rb').read()
n = struct.unpack('>H', data[:2])[0]
entries, off = {}, 2
for _ in range(n):
    nl, kl = struct.unpack('>HH', data[off:off+4]); off += 4
    name = data[off:off+nl]; off += nl
    key = data[off:off+kl]; off += kl
    entries[name] = key
assert off == len(data), "bundle length mismatch"
print(f"bundle: {n} certificates, {len(data)} bytes")

import subprocess, re
failed = False
for host in ["api.spotify.com", "accounts.spotify.com", "i.scdn.co", "api.open-meteo.com"]:
    out = subprocess.run(["openssl","s_client","-connect",f"{host}:443","-servername",host,"-showcerts"],
                         input=b"", capture_output=True).stdout.decode()
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", out, re.S)
    chain = [x509.load_pem_x509_certificate(p.encode()) for p in pems]
    # intermediates must chain to each other (leaf signed by chain[1], ...) -- handled by mbedtls;
    # here check the last one against the bundle
    last = chain[-1]
    issuer_der = last.issuer.public_bytes()
    key_der = entries.get(issuer_der)
    if key_der is None:
        print(f"{host}: FAIL issuer not in bundle: {last.issuer.rfc4514_string()}"); failed = True; continue
    pub = serialization.load_der_public_key(key_der)
    if isinstance(pub, ec.EllipticCurvePublicKey):
        pub.verify(last.signature, last.tbs_certificate_bytes, ec.ECDSA(last.signature_hash_algorithm))
    else:
        pub.verify(last.signature, last.tbs_certificate_bytes, padding.PKCS1v15(), last.signature_hash_algorithm)
    print(f"{host}: OK  ({len(chain)} certs sent, top link signed by bundled '{last.issuer.rfc4514_string()}')")
sys.exit(1 if failed else 0)

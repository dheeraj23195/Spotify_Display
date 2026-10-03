#pragma once
#include <WiFiClientSecure.h>

// Makes a TLS client verify servers against the root CA bundle embedded in the
// firmware (data/cert/x509_crt_bundle.bin). Call once per client, before connecting.
// There is no insecure fallback: without the bundle the client refuses to connect.
// Certificates have dates, so callers must also check timeIsSet() before each request.
void tlsConfigure(WiFiClientSecure &client);

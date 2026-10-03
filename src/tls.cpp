#include "tls.h"

// Embedded by board_build.embed_files in platformio.ini
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

void tlsConfigure(WiFiClientSecure &client) {
  client.setCACertBundle(rootca_crt_bundle_start);
  client.setHandshakeTimeout(10);  // give up on a stalled handshake after 10 s
}

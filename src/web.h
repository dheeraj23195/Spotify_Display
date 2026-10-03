#pragma once

// Starts the settings page, firmware upload page and Wi-Fi code push.
// Call once Wi-Fi is connected.
void webBegin();

// True while a firmware file is being uploaded through the web page.
bool webUploadActive();
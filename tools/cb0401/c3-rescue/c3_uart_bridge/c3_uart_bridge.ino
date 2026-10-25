// ESP32-C3 WiFi rescue console for Xiaomi CB0401 (IPQ5018).
// C3 hosts a WiFi AP and bridges it to the router UART (serial over WiFi).
//
//   WiFi AP : SSID "cb0401-rescue", passphrase from secrets.h (TX capped 7.5 dBm)
//             Up for 90 s after power-on; if nobody joins by then WiFi goes
//             off until the next power-on. Once someone joins it stays up.
//   Console : connect to the AP, PuTTY (Raw) / nc  192.168.4.1 : 2323
//             (a new TCP connection takes over; USB-CDC console works too)
//   OTA     : http://192.168.4.1/  (user "cb0401", pass = the AP passphrase)
//             upload the APP image (c3_uart_bridge.ino.bin, NOT merged.bin)
//
// It auto-interrupts U-Boot autoboot and, at the prompt, boots per the mode:
//   NAND (default): the stored bootcmd (NVS, see !bootcmd)
//   !tftp         : wait for the eth link, then tftpboot ow.itb + bootm (RAM
//                   only, no NAND write), retrying; NAND fallback if it fails
//   !hold         : stay at the U-Boot prompt
//
// Magic (LOCKED until the unlock string "::cb0401-magic::" is typed on either
// console; it only keeps stray console text from triggering commands):
//   !dump !clear !hold !tftp !auto !nand !dl !lock !status
//   !bootcmd <cmd>   store the NAND boot command (Enter saves)
//   !bootcmd         (empty) show it;  !bootcmd default  restore built-in
// Wiring: C3 GPIO7=TX ->divider-> router RX ; router TX -> C3 GPIO6 (direct).

#include <Arduino.h>
#include "driver/uart.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "secrets.h"   // CB_AP_PASS (see secrets.h.example)

#define PIN_TX 7
#define PIN_RX 6
#define BAUD   115200
#define RX_INVERT 0

const char*    AP_SSID  = "cb0401-rescue";
const char*    AP_PASS  = CB_AP_PASS;
const uint16_t TCP_PORT = 2323;
const uint32_t AP_IDLE_OFF = 90000;     // ms: WiFi off if nobody joined by then

const char* TRIG    = "stop autoboot";
const char* PROMPT  = "IPQ5018#";
const char* BANNER  = "Format: Log Type";
const char  INTKEY  = ' ';
const char* BOOTCMD_DEFAULT = "nand read 0x44000000 0x2980000 0x1f00000 ; bootm 0x44000000";
const char* TFTPCMD = "tftpboot 0x44000000 ow.itb ; bootm 0x44000000\r";
const char* UNLOCK  = "::cb0401-magic::";
const char* OTA_USER = "cb0401";
const size_t BOOTCMD_MAX = 200;         // U-Boot hung on a ~400 char line

const uint32_t TFTP_LINK_WAIT = 8000;   // ms to let the eth link train before 1st tftpboot
const uint32_t TFTP_RETRY_GAP = 6000;   // ms between retries
const int      TFTP_MAX_RETRY = 3;

HardwareSerial Router(1);
WiFiServer server(TCP_PORT);
WiFiClient client;
WebServer web(80);
Preferences prefs;

String win, uwin, bootcmd, cap;
bool spamming = false, done = false, hold = false, unlocked = false;
bool tftpmode = false, awaitingTftp = false;
bool apOn = false, apKept = false, capturing = false, otaReject = false;
int  tftpRetries = 0;
uint32_t lastSpam = 0, tftpAt = 0, rxTotal = 0, tftpStartAt = 0;
bool tftpPending = false;

// router -> console output is collected and sent in one go: one TCP write per
// byte (with Nagle off) floods the weak AP and blocks the loop
static uint8_t obuf[512];
size_t olen = 0;

#define LOGCAP (48 * 1024)
static char logbuf[LOGCAP];
size_t loglen = 0; bool logfull = false;
void logByte(char c){ if (loglen < LOGCAP){ logbuf[loglen++] = c; if (loglen == LOGCAP) logfull = true; } }
void logStr(const char* s){ while (*s) logByte(*s++); }
void pushWin(char c){ win += c; if (win.length() > 80) win.remove(0, win.length()-80); }

void cliWrite(const uint8_t* b, size_t n){ if (client && client.connected()) client.write(b, n); }
void outRaw(const uint8_t* b, size_t n){ Serial.write(b, n); cliWrite(b, n); }
void flushOut(){ if (olen){ outRaw(obuf, olen); olen = 0; } }
void outStr(const char* s){ flushOut(); Serial.print(s); cliWrite((const uint8_t*)s, strlen(s)); }

void enterDownloadMode(){
  outStr("\r\n[C3] -> ROM download mode. Flash now (file @ 0x0).\r\n");
  Serial.flush(); delay(200);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

void dumpLog(){
  outStr("\r\n[C3] ===== BOOT LOG DUMP =====\r\n");
  size_t i = 0;
  while (i < loglen){ size_t n = (loglen - i > 128) ? 128 : (loglen - i);
    Serial.write((const uint8_t*)logbuf + i, n);
    if (client && client.connected()) client.write((const uint8_t*)logbuf + i, n);
    i += n; delay(3); }
  outStr(logfull ? "\r\n[C3] (buffer FULL -> truncated)\r\n" : "\r\n[C3] ===== END =====\r\n");
}

void sendBoot(){ Router.print(bootcmd); Router.print('\r'); logStr(bootcmd.c_str()); logStr("\r"); }

void showStatus(){
  String s = "\r\n[C3] fw " __DATE__ " " __TIME__ "\r\n[C3] mode: ";
  s += hold ? "HOLD" : (tftpmode ? "TFTP" : "NAND");
  s += "\r\n[C3] bootcmd: " + bootcmd + "\r\n[C3] wifi: ";
  s += apOn ? (apKept ? "on (kept until power-off)" : "on (90 s window)") : "off";
  s += "\r\n";
  outStr(s.c_str());
}

void wifiOff(){
  if (client) client.stop();
  web.stop(); server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apOn = false;
  outStr("\r\n[C3] nobody joined the AP in 90 s -> WiFi off until power-off\r\n");
  logStr("\r\n[C3] >>> WiFi off (idle) <<<\r\n");
}

void finishBootcmd(){
  capturing = false;
  cap.trim();
  if (cap.length() == 0){ outStr(("\r\n[C3] bootcmd: " + bootcmd + "\r\n").c_str()); return; }
  if (cap == "default"){ prefs.remove("bootcmd"); bootcmd = BOOTCMD_DEFAULT; }
  else { prefs.putString("bootcmd", cap); bootcmd = cap; }
  outStr(("\r\n[C3] bootcmd saved: " + bootcmd + "\r\n").c_str());
}

void rearm(){ spamming = false; done = false; awaitingTftp = false; tftpPending = false; tftpRetries = 0; win = ""; }

void sendTftp(){ outStr("\r\n[C3] TFTP RAM boot attempt...\r\n"); delay(300); Router.print(TFTPCMD); logStr(TFTPCMD); awaitingTftp = true; tftpAt = millis(); win = ""; }

void handleInput(char c){
  if (capturing){
    if (c == '\r' || c == '\n'){ finishBootcmd(); return; }
    if ((c == 8 || c == 127) && cap.length()){ cap.remove(cap.length()-1); outStr("\b \b"); return; }
    if (c >= 32 && cap.length() < BOOTCMD_MAX){ cap += c; char e[2] = { c, 0 }; outStr(e); }
    return;
  }
  uwin += c; if (uwin.length() > 40) uwin.remove(0, uwin.length()-40);
  if (uwin.indexOf(UNLOCK) >= 0){ uwin = ""; unlocked = true; outStr("\r\n[C3] magic UNLOCKED. !dump !clear !hold !tftp !auto !dl !status !bootcmd !lock\r\n"); return; }
  if (unlocked){
    if (uwin.indexOf("!lock")  >= 0){ uwin = ""; unlocked = false; outStr("\r\n[C3] magic locked.\r\n"); return; }
    if (uwin.indexOf("!dump")  >= 0){ uwin = ""; dumpLog(); return; }
    if (uwin.indexOf("!clear") >= 0){ uwin = ""; loglen = 0; logfull = false; outStr("\r\n[C3] log cleared\r\n"); return; }
    if (uwin.indexOf("!hold")  >= 0){ uwin = ""; hold = true;  tftpmode = false; outStr("\r\n[C3] HOLD: next boot stops at U-Boot prompt\r\n"); return; }
    if (uwin.indexOf("!tftp")  >= 0){ uwin = ""; tftpmode = true; hold = false; outStr("\r\n[C3] TFTP: next boot waits for link then tftpboot ow.itb (RAM), NAND fallback\r\n"); return; }
    if (uwin.indexOf("!auto")  >= 0){ uwin = ""; hold = false; tftpmode = false; outStr("\r\n[C3] AUTO/NAND: next boot runs the NAND bootcmd\r\n"); return; }
    if (uwin.indexOf("!nand")  >= 0){ uwin = ""; hold = false; tftpmode = false; outStr("\r\n[C3] NAND: next boot runs the NAND bootcmd\r\n"); return; }
    if (uwin.indexOf("!status")>= 0){ uwin = ""; showStatus(); return; }
    if (uwin.indexOf("!bootcmd")>= 0){ uwin = ""; capturing = true; cap = ""; outStr("\r\n[C3] bootcmd> "); return; }
    if (uwin.indexOf("!dl")    >= 0){ uwin = ""; enterDownloadMode(); return; }
  }
  Router.write(c);
}

// --- OTA over the AP ---------------------------------------------------------
bool otaAuth(){ return web.authenticate(OTA_USER, AP_PASS); }

void otaPage(){
  if (!otaAuth()) return web.requestAuthentication();
  String p = "<html><body><h3>cb0401-rescue OTA</h3><p>running fw " __DATE__ " " __TIME__ "</p>"
             "<p>Upload the app image <b>c3_uart_bridge.ino.bin</b> (not merged.bin).</p>"
             "<form method=POST action=/update enctype=multipart/form-data>"
             "<input type=file name=fw> <input type=submit value=Flash></form></body></html>";
  web.send(200, "text/html", p);
}

void otaDone(){
  if (!otaAuth()) return web.requestAuthentication();
  bool ok = !otaReject && !Update.hasError();
  web.send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting the C3\n" : "FAILED, old firmware kept\n");
  if (ok){ outStr("\r\n[C3] OTA ok -> rebooting C3\r\n"); delay(500); ESP.restart(); }
}

void otaUpload(){
  HTTPUpload& up = web.upload();
  if (up.status == UPLOAD_FILE_START){
    otaReject = !otaAuth();
    if (!otaReject){ outStr("\r\n[C3] OTA upload started\r\n"); Update.begin(UPDATE_SIZE_UNKNOWN); }
  } else if (up.status == UPLOAD_FILE_WRITE){
    if (otaReject) return;
    // first chunk: must be an app image (0xE9 header + esp_app_desc magic at
    // 0x20), so a merged.bin (starts with the bootloader) is refused
    if (up.totalSize == 0 && (up.currentSize < 0x24 || up.buf[0] != 0xE9 ||
        *(uint32_t*)(up.buf + 0x20) != 0xABCD5432)){
      otaReject = true; Update.abort(); outStr("\r\n[C3] OTA refused: not an app image\r\n"); return;
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaReject = true;
  } else if (up.status == UPLOAD_FILE_END){
    if (!otaReject && !Update.end(true)) otaReject = true;
    if (otaReject) outStr("\r\n[C3] OTA failed, old firmware kept\r\n");
  } else if (up.status == UPLOAD_FILE_ABORTED){
    Update.abort(); otaReject = true;
  }
}

void setup(){
  Serial.begin(BAUD);
  // without a USB host the CDC buffer fills up and every write would wait
  // for it: drop the output instead of stalling the bridge
  Serial.setTxTimeoutMs(0);
  Router.setRxBufferSize(8192);
  Router.begin(BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
#if RX_INVERT
  uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV);
#endif
  loglen = 0; logfull = false;
  prefs.begin("cb0401", false);
  bootcmd = prefs.getString("bootcmd", BOOTCMD_DEFAULT);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  esp_wifi_set_max_tx_power(30);   // 7.5 dBm: C3 Super Mini stability
  server.begin();
  server.setNoDelay(true);
  web.on("/", HTTP_GET, otaPage);
  web.on("/update", HTTP_POST, otaDone, otaUpload);
  web.begin();
  apOn = true;
  delay(200);
  Serial.print("\r\n[C3] rescue ready. AP '"); Serial.print(AP_SSID);
  Serial.print("' -> raw "); Serial.print(WiFi.softAPIP()); Serial.print(":"); Serial.println(TCP_PORT);
  logStr("[C3] === power-on, capture starts here ===\r\n");
}

void loop(){
  // AP window: off after 90 s unless someone joined; once joined it stays up
  if (apOn && !apKept){
    if (WiFi.softAPgetStationNum() > 0){ apKept = true; logStr("\r\n[C3] >>> AP joined -> WiFi kept <<<\r\n"); }
    else if (millis() > AP_IDLE_OFF) wifiOff();
  }

  // a NEW TCP connection always takes over (lets you reconnect after a drop)
  if (apOn && server.hasClient()){
    WiFiClient nc = server.accept();
    if (client) client.stop();
    client = nc;
    client.setNoDelay(true);
    client.print("\r\n[C3] *** rescue console *** magic LOCKED\r\n");
    client.print("[C3] modes: !nand (default) !tftp !hold  | Enter for the router prompt\r\n\r\n");
  }
  if (apOn) web.handleClient();

  while (Router.available()){
    char c = Router.read();
    rxTotal++;
    obuf[olen++] = (uint8_t)c;
    if (olen == sizeof(obuf)) flushOut();
    logByte(c); pushWin(c);
    if (done && win.indexOf(BANNER) >= 0){ logStr("\r\n[C3] >>> new boot -> re-armed <<<\r\n"); rearm(); }
    if (!done && !spamming && win.indexOf(TRIG) >= 0){ spamming = true; outStr("\r\n[C3] autoboot -> interrupting...\r\n"); logStr("\r\n[C3] >>> interrupting <<<\r\n"); }
    if (spamming && win.indexOf(PROMPT) >= 0){
      spamming = false; done = true;
      if (hold){ outStr("\r\n[C3] HOLD at U-Boot prompt.\r\n"); }
      else if (tftpmode){
        outStr("\r\n[C3] at prompt -> waiting ~8s for the eth link, then TFTP RAM boot.\r\n");
        tftpRetries = 0;
        tftpPending = true;
        tftpStartAt = millis() + TFTP_LINK_WAIT;
      }
      else { outStr("\r\n[C3] at prompt -> booting from NAND.\r\n"); delay(300); sendBoot(); }
    }
  }
  flushOut();

  // first TFTP attempt once the eth link had time to come up (no delay():
  // the console keeps working meanwhile)
  if (tftpPending && (int32_t)(millis() - tftpStartAt) >= 0){ tftpPending = false; sendTftp(); }

  // TFTP retry / NAND fallback -- in the MAIN loop so it fires even when the
  // router is idle at the prompt (tftpboot failed, link not up yet).
  if (awaitingTftp && millis() - tftpAt > TFTP_RETRY_GAP){
    if (win.indexOf(PROMPT) >= 0){
      win = "";
      if (tftpRetries < TFTP_MAX_RETRY){
        tftpRetries++;
        char m[64]; snprintf(m, sizeof(m), "\r\n[C3] TFTP not up, retry %d/%d...\r\n", tftpRetries, TFTP_MAX_RETRY);
        outStr(m);
        delay(300); Router.print(TFTPCMD); logStr(TFTPCMD); tftpAt = millis();
      } else {
        awaitingTftp = false;
        outStr("\r\n[C3] TFTP failed -> NAND fallback.\r\n"); logStr("\r\n[C3] >>> TFTP failed -> NAND <<<\r\n");
        delay(200); sendBoot();
      }
    }
  }

  while (Serial.available()) handleInput((char)Serial.read());
  if (client && client.connected()){ while (client.available()) handleInput((char)client.read()); }

  if (spamming && millis() - lastSpam > 20){ Router.write(INTKEY); lastSpam = millis(); }
}

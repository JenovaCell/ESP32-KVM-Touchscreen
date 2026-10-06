#include "ble_kbd.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>

namespace kbd {
namespace {

// Keyboard: report ID 1, 8 bytes in (modifiers, reserved, 6 keys), 1 byte LEDs out.
// Consumer control (media keys): report ID 2, one 16-bit usage (KVM-6).
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0};

// Roles: 0 = Work host, 1 = Game host. Stored identity address per role.
const char *kPeerKey[2] = {"idW", "idG"};

constexpr int kMaxConns = 3;

struct Conn {
  bool used;
  bool authed;
  bool weClosed;       // we hung up on purpose (policy), as opposed to the host dropping
  uint16_t handle;
  int8_t role;
  uint32_t authedAt;   // millis() when encryption came up
  char addr[20];
};

NimBLEServer *server = nullptr;
NimBLECharacteristic *input = nullptr;
NimBLECharacteristic *consumer = nullptr;
Preferences prefs;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Conn conns[kMaxConns];

char lastEventText[48] = "none yet";
char prevEventText[48] = "";
char pendingReason[40] = "";
volatile bool pendingReasonValid = false;

// Remember the last two events for the screen, each stamped with seconds since boot.
void setEvent(const char *text) {
  strlcpy(prevEventText, lastEventText, sizeof(prevEventText));
  snprintf(lastEventText, sizeof(lastEventText), "%lus %s", millis() / 1000UL, text);
}

Slot slot = Slot::None;
volatile bool policyDirty = false;
volatile bool changed = false;
volatile bool havePass = false;
volatile uint32_t pass = 0;

Conn *findLocked(uint16_t handle) {
  for (auto &c : conns)
    if (c.used && c.handle == handle) return &c;
  return nullptr;
}

constexpr uint16_t kNoConn = 0xFFFF;

// Connection handle of the HID host for the current target, or kNoConn. Both hosts stay
// connected all the time (KVM-23); only this one receives key reports.
uint16_t activeHandle() {
  if (slot == Slot::None) return kNoConn;
  uint16_t h = kNoConn;
  portENTER_CRITICAL(&mux);
  for (auto &c : conns)
    if (c.used && c.authed && c.role == static_cast<int8_t>(slot)) h = c.handle;
  portEXIT_CRITICAL(&mux);
  return h;
}

// Is the HID host for the current target connected?
bool hostLinked() { return activeHandle() != kNoConn; }

// Hang up on purpose and remember why (shown on the screen for diagnosis).
void closeConn(uint16_t handle, const char *why) {
  portENTER_CRITICAL(&mux);
  Conn *c = findLocked(handle);
  if (c) c->weClosed = true;
  portEXIT_CRITICAL(&mux);
  setEvent(why);
  Serial.printf("refusing connection: %s\n", why);
  server->disconnect(handle);
}

// Hosts may use a temporary address when encryption starts and only reveal their
// permanent (identity) address a moment later, so wait before deciding.
const uint32_t kIdentityWaitMs = 600;

// Decide what each authenticated connection is. Known hosts (work, game) are accepted on any
// screen, so switching target needs no reconnect. An unknown device that pairs becomes the host
// for the active target, if that role is still free; on the MAC screen nobody can pair.
void applyPolicy() {
  Conn snap[kMaxConns];
  portENTER_CRITICAL(&mux);
  memcpy(snap, conns, sizeof(conns));
  portEXIT_CRITICAL(&mux);

  bool waiting = false;
  for (auto &c : snap) {
    if (!c.used || !c.authed) continue;
    if (millis() - c.authedAt < kIdentityWaitMs) {
      waiting = true;
      continue;
    }
    // Use the identity address as the stack knows it now, not the early one.
    String addr = c.addr;
    ble_gap_conn_desc d;
    if (ble_gap_conn_find(c.handle, &d) == 0)
      addr = NimBLEAddress(d.peer_id_addr).toString().c_str();

    int role = -1;
    for (int r = 0; r < 2; r++)
      if (prefs.getString(kPeerKey[r], "") == addr) role = r;
    if (role < 0) {
      if (slot == Slot::None) {
        closeConn(c.handle, "refused: pair on WORK/GAME screen");
        continue;
      }
      const int want = static_cast<int>(slot);
      if (prefs.getString(kPeerKey[want], "").length() == 0) {
        prefs.putString(kPeerKey[want], addr);
        role = want;
        setEvent(want == 0 ? "paired: work host" : "paired: game host");
      } else {
        closeConn(c.handle, "refused: unknown device");
        continue;
      }
    }
    bool newlyKnown = false;
    portENTER_CRITICAL(&mux);
    Conn *p = findLocked(c.handle);
    if (p) {
      newlyKnown = (p->role != role);
      p->role = static_cast<int8_t>(role);
      strlcpy(p->addr, addr.c_str(), sizeof(p->addr));
    }
    portEXIT_CRITICAL(&mux);
    if (newlyKnown) setEvent(role == 0 ? "work host connected" : "game host connected");
  }
  if (waiting) policyDirty = true;  // look again shortly
  changed = true;
}

// Plain-words names for the most common Bluetooth disconnect reason codes.
const char *hciReasonText(int code) {
  switch (code) {
    case 0x05: return "auth failure";
    case 0x06: return "key missing";
    case 0x08: return "link timeout";
    case 0x13: return "remote ended";
    case 0x14: return "remote low resources";
    case 0x15: return "remote power off";
    case 0x16: return "we ended";
    case 0x1A: return "unsupported feature";
    case 0x3D: return "MIC failure";
    case 0x3E: return "connect failed";
    default: return "other";
  }
}

// Listens to low-level link events so the screen can say WHY a link ended.
ble_gap_event_listener gapListener;

int onGapEvent(struct ble_gap_event *event, void *) {
  switch (event->type) {
    case BLE_GAP_EVENT_DISCONNECT: {
      const int r = event->disconnect.reason;
      const int code = (r >= BLE_HS_ERR_HCI_BASE) ? (r - BLE_HS_ERR_HCI_BASE) : r;
      snprintf(pendingReason, sizeof(pendingReason), "dropped: %s (0x%02X)", hciReasonText(code), code);
      pendingReasonValid = true;
      Serial.printf("disconnect reason: %s (0x%02X)\n", hciReasonText(code), code);
      break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
      if (event->enc_change.status != 0) {
        char text[40];
        snprintf(text, sizeof(text), "encryption failed (%d)", event->enc_change.status);
        setEvent(text);
        Serial.printf("%s\n", text);
      }
      break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
      setEvent("repeat pairing: bond replaced");
      Serial.println("repeat pairing: a device paired again; the old bond is replaced");
      break;
    default:
      break;
  }
  return 0;
}

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    portENTER_CRITICAL(&mux);
    for (auto &c : conns) {
      if (c.used) continue;
      c.used = true;
      c.authed = false;
      c.weClosed = false;
      c.authedAt = 0;
      c.role = -1;
      c.handle = desc->conn_handle;
      strlcpy(c.addr, NimBLEAddress(desc->peer_id_addr).toString().c_str(), sizeof(c.addr));
      break;
    }
    portEXIT_CRITICAL(&mux);
    setEvent("link up, not yet encrypted");
    changed = true;
  }
  void onDisconnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    portENTER_CRITICAL(&mux);
    Conn *c = findLocked(desc->conn_handle);
    const bool ours = c && c->weClosed;
    if (c) c->used = false;
    portEXIT_CRITICAL(&mux);
    if (!ours) {
      setEvent(pendingReasonValid ? pendingReason : "dropped by host/link");
      Serial.println("connection dropped by the host or the link");
    }
    pendingReasonValid = false;
    changed = true;  // also ends any pairing overlay
  }
  uint32_t onPassKeyRequest() override {
    pass = 100000 + (esp_random() % 900000);
    havePass = true;
    return pass;
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    if (!desc->sec_state.encrypted || !desc->sec_state.authenticated) {
      closeConn(desc->conn_handle, "pairing failed");
      return;
    }
    portENTER_CRITICAL(&mux);
    Conn *c = findLocked(desc->conn_handle);
    if (c) {
      strlcpy(c->addr, NimBLEAddress(desc->peer_id_addr).toString().c_str(), sizeof(c->addr));
      c->authed = true;
      c->authedAt = millis();
    }
    portEXIT_CRITICAL(&mux);
    setEvent("encrypted, checking host");
    policyDirty = true;
  }
};

// Sends one 8-byte HID report to ONE connected host. (characteristic->notify() would send it
// to every subscribed host, and the other PC must not receive these keys.)
bool notifyChr(uint16_t handle, NimBLECharacteristic *chr, const uint8_t *data, size_t len) {
  if (chr == nullptr) return false;
  struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
  if (om == nullptr) return false;
  return ble_gatts_notify_custom(handle, chr->getHandle(), om) == 0;
}

bool notifyTo(uint16_t handle, const uint8_t *report) { return notifyChr(handle, input, report, 8); }

void sendReport(uint8_t mods, uint8_t key) {
  uint8_t r[8] = {mods, 0, key, 0, 0, 0, 0, 0};
  const uint16_t h = activeHandle();
  if (h != kNoConn) notifyTo(h, r);
}

volatile uint32_t statRx = 0, statTxOk = 0, statTxFail = 0, statNoHost = 0, statBad = 0;

bool keyFor(char c, uint8_t &mods, uint8_t &key) {
  mods = 0;
  if (c >= 'a' && c <= 'z') {
    key = 4 + (c - 'a');
  } else if (c >= 'A' && c <= 'Z') {
    key = 4 + (c - 'A');
    mods = 0x02;  // left shift
  } else if (c >= '1' && c <= '9') {
    key = 30 + (c - '1');
  } else if (c == '0') {
    key = 39;
  } else if (c == ' ') {
    key = 0x2C;
  } else if (c == '\n') {
    key = 0x28;
  } else {
    return false;
  }
  return true;
}

}  // namespace

void begin() {
  prefs.begin("kvmble", false);

  NimBLEDevice::init("Desk Keyboard");
  const int listenRc = ble_gap_event_listener_register(&gapListener, onGapEvent, nullptr);
  if (listenRc != 0) Serial.printf("gap listener register failed: %d\n", listenRc);
  // Pairings are never wiped automatically (KVM-15): a firmware update keeps them.
  // The Mac app used to be a paired Bluetooth device (roles in earlier versions). It now
  // talks over the USB cable, so forget its old pairing and keep the host pairings.
  const String oldMac = prefs.getString("idM", "");
  if (oldMac.length() > 0) {
    for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
      NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
      if (String(a.toString().c_str()) == oldMac) NimBLEDevice::deleteBond(a);
    }
    prefs.remove("idM");
  }
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  server->advertiseOnDisconnect(false);  // poll() decides when to advertise

  NimBLEHIDDevice *hid = new NimBLEHIDDevice(server);
  input = hid->inputReport(1);
  consumer = hid->inputReport(2);
  hid->outputReport(1);
  hid->manufacturer()->setValue("DIY");
  hid->pnp(0x02, 0x303A, 0x4B56, 0x0100);  // Espressif VID, hobby PID
  hid->hidInfo(0x00, 0x01);
  hid->reportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
  hid->startServices();
  hid->setBatteryLevel(100);

  // Advertising data: keyboard appearance, HID service and name.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setAppearance(0x03C1);  // keyboard
  ad.setCompleteServices(NimBLEUUID(static_cast<uint16_t>(0x1812)));
  ad.setName("Desk Keyboard");
  adv->setAdvertisementData(ad);
  adv->setMinInterval(32);  // 20 ms, so hosts reconnect quickly
  adv->setMaxInterval(48);
}

void setSlot(Slot s) {
  if (s != slot) {
    // Release anything held on the host we are leaving, so no key stays pressed there.
    const uint16_t old = activeHandle();
    if (old != kNoConn) {
      static const uint8_t allUp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      static const uint8_t noMedia[2] = {0, 0};
      notifyTo(old, allUp);
      notifyChr(old, consumer, noMedia, 2);
    }
  }
  slot = s;
  policyDirty = true;
  changed = true;
}

void poll() {
  if (policyDirty) {
    policyDirty = false;
    applyPolicy();
  }
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  // Always advertise while there is room, on every screen, so the work and game PCs reconnect
  // by themselves and are already linked when you switch to them.
  const bool room = server->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
  if (room && !adv->isAdvertising()) adv->start();
  if (!room && adv->isAdvertising()) adv->stop();
}

bool connected() { return hostLinked(); }

// Sends one 8-byte HID report (from the Mac app, over USB) to the connected host.
bool relayReport(const uint8_t *report) {
  statRx++;
  if (!hostLinked()) {
    statNoHost++;
    return false;
  }
  if (notifyTo(activeHandle(), report)) {
    statTxOk++;
    return true;
  }
  statTxFail++;
  return false;
}

// One media/consumer usage (0 = released) to the current target's PC only.
bool relayConsumer(uint16_t usage) {
  const uint16_t h = activeHandle();
  if (h == kNoConn) return false;
  const uint8_t d[2] = {static_cast<uint8_t>(usage & 0xFF), static_cast<uint8_t>(usage >> 8)};
  return notifyChr(h, consumer, d, 2);
}

KeyStats keyStats() { return {statRx, statTxOk, statTxFail, statNoHost, statBad}; }

const char *lastEvent() { return lastEventText; }

const char *prevEvent() { return prevEventText; }

int bondCount() { return NimBLEDevice::getNumBonds(); }

bool takePasskey(uint32_t &passkey) {
  if (!havePass) return false;
  havePass = false;
  passkey = pass;
  return true;
}

bool takeChanged() {
  if (!changed) return false;
  changed = false;
  return true;
}

void typeText(const char *text) {
  if (!hostLinked()) return;
  for (; *text; ++text) {
    uint8_t mods, key;
    if (!keyFor(*text, mods, key)) continue;
    sendReport(mods, key);
    delay(12);
    sendReport(0, 0);
    delay(12);
  }
}

void forgetCurrentHost() {
  if (slot == Slot::None) return;  // nothing to forget on the MAC screen
  const int role = static_cast<int>(slot);
  const String mine = prefs.getString(kPeerKey[role], "");
  if (mine.length() > 0) {
    for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
      NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
      if (String(a.toString().c_str()) == mine) NimBLEDevice::deleteBond(a);
    }
    prefs.remove(kPeerKey[role]);
  }
  Conn snap[kMaxConns];
  portENTER_CRITICAL(&mux);
  memcpy(snap, conns, sizeof(conns));
  portEXIT_CRITICAL(&mux);
  for (auto &c : snap)
    if (c.used && String(c.addr) == mine) server->disconnect(c.handle);
  changed = true;
}

}  // namespace kbd

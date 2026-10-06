#include "ble_kbd.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>

namespace kbd {
namespace {

// Keyboard: report ID 1, 8 bytes in (modifiers, reserved, 6 keys), 1 byte LEDs out.
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0};

// Control service used by the Mac app.
const char *kCtlSvc = "7d1b0001-5a3c-4f8e-9c1d-4b6a2e0f1a01";
const char *kKeysUuid = "7d1b0002-5a3c-4f8e-9c1d-4b6a2e0f1a01";   // write: 8-byte HID report
const char *kCmdUuid = "7d1b0003-5a3c-4f8e-9c1d-4b6a2e0f1a01";    // write: 1 = step left, 2 = step right
const char *kStateUuid = "7d1b0004-5a3c-4f8e-9c1d-4b6a2e0f1a01";  // read/notify: active target

#ifndef KVM_VERSION
#define KVM_VERSION "dev"
#endif
#ifndef KVM_BUILD
#define KVM_BUILD "dev"
#endif

const char *kVerUuid = "7d1b0005-5a3c-4f8e-9c1d-4b6a2e0f1a01";    // read: firmware version text

// Roles: 0 = Work host, 1 = Game host, 2 = Mac app. Stored address per role.
const char *kPeerKey[3] = {"peerW", "peerG", "peerM"};
const int kMacRole = 2;

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
NimBLECharacteristic *stateChr = nullptr;
Preferences prefs;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Conn conns[kMaxConns];

char lastEventText[40] = "none yet";

void setEvent(const char *text) { strlcpy(lastEventText, text, sizeof(lastEventText)); }

Slot slot = Slot::None;
volatile bool policyDirty = false;
volatile bool changed = false;
volatile bool havePass = false;
volatile uint32_t pass = 0;
volatile int pendingStep = 0;

Conn *findLocked(uint16_t handle) {
  for (auto &c : conns)
    if (c.used && c.handle == handle) return &c;
  return nullptr;
}

int roleOfHandle(uint16_t handle) {
  int role = -1;
  portENTER_CRITICAL(&mux);
  Conn *c = findLocked(handle);
  if (c && c->authed) role = c->role;
  portEXIT_CRITICAL(&mux);
  return role;
}

// Is the HID host for the current slot connected?
bool hostLinked() {
  if (slot == Slot::None) return false;
  bool ok = false;
  portENTER_CRITICAL(&mux);
  for (auto &c : conns)
    if (c.used && c.authed && c.role == static_cast<int8_t>(slot)) ok = true;
  portEXIT_CRITICAL(&mux);
  return ok;
}

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

// Decide what each authenticated connection is allowed to be.
// An unknown device that pairs becomes the host for the active target
// (or the Mac app when the Mac screen is active), if that role is still free.
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
    for (int r = 0; r < 3; r++)
      if (prefs.getString(kPeerKey[r], "") == addr) role = r;
    if (role < 0) {
      const int want = (slot == Slot::None) ? kMacRole : static_cast<int>(slot);
      if (prefs.getString(kPeerKey[want], "").length() == 0) {
        prefs.putString(kPeerKey[want], addr);
        role = want;
        setEvent(want == kMacRole ? "paired: Mac app" : (want == 0 ? "paired: work host" : "paired: game host"));
      } else {
        closeConn(c.handle, "refused: unknown device");
        continue;
      }
    }
    const bool allowed = (role == kMacRole) || (role == static_cast<int>(slot));
    if (!allowed) {
      closeConn(c.handle, "refused: not this target");
      continue;
    }
    portENTER_CRITICAL(&mux);
    Conn *p = findLocked(c.handle);
    if (p) {
      p->role = role;
      strlcpy(p->addr, addr.c_str(), sizeof(p->addr));
    }
    portEXIT_CRITICAL(&mux);
    if (role != kMacRole) setEvent("host connected");
  }
  if (waiting) policyDirty = true;  // look again shortly
  changed = true;
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
  }
  void onDisconnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    portENTER_CRITICAL(&mux);
    Conn *c = findLocked(desc->conn_handle);
    const bool ours = c && c->weClosed;
    if (c) c->used = false;
    portEXIT_CRITICAL(&mux);
    if (!ours) {
      setEvent("dropped by host/link");
      Serial.println("connection dropped by the host or the link");
    }
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

void sendReport(uint8_t mods, uint8_t key) {
  uint8_t r[8] = {mods, 0, key, 0, 0, 0, 0, 0};
  input->setValue(r, sizeof(r));
  input->notify();
}

// The Mac app writes a ready-made 8-byte HID report; it is relayed to the host.
class KeysCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (roleOfHandle(desc->conn_handle) != kMacRole) return;
    const std::string v = c->getValue();
    if (v.size() != 8 || !hostLinked()) return;
    input->setValue(reinterpret_cast<const uint8_t *>(v.data()), 8);
    input->notify();
  }
};

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (roleOfHandle(desc->conn_handle) != kMacRole) return;
    const std::string v = c->getValue();
    if (v.empty()) return;
    if (v[0] == 1) pendingStep = -1;
    else if (v[0] == 2) pendingStep = +1;
  }
};

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
  if (prefs.getUChar("schema", 0) < kSchema) {
    // Earlier versions stored hosts by an address that could be the wrong one.
    // Start clean: every device has to pair again once.
    NimBLEDevice::deleteAllBonds();
    prefs.remove("peerW");
    prefs.remove("peerG");
    prefs.remove("peerM");
    prefs.putUChar("schema", kSchema);
  }
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  server->advertiseOnDisconnect(false);  // poll() decides when to advertise

  NimBLEHIDDevice *hid = new NimBLEHIDDevice(server);
  input = hid->inputReport(1);
  hid->outputReport(1);
  hid->manufacturer()->setValue("DIY");
  hid->pnp(0x02, 0x303A, 0x4B56, 0x0100);  // Espressif VID, hobby PID
  hid->hidInfo(0x00, 0x01);
  hid->reportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEService *ctl = server->createService(kCtlSvc);
  NimBLECharacteristic *keys = ctl->createCharacteristic(
      kKeysUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  keys->setCallbacks(new KeysCb());
  NimBLECharacteristic *cmd = ctl->createCharacteristic(
      kCmdUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  cmd->setCallbacks(new CmdCb());
  stateChr = ctl->createCharacteristic(
      kStateUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN |
                      NIMBLE_PROPERTY::NOTIFY);
  const uint8_t zero = 0;
  stateChr->setValue(&zero, 1);
  NimBLECharacteristic *ver = ctl->createCharacteristic(
      kVerUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  ver->setValue(std::string(KVM_VERSION " " KVM_BUILD));
  ctl->start();

  // Advertising data: keyboard appearance, HID service and name. The control
  // service goes in the scan response (it does not fit alongside the name).
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setAppearance(0x03C1);  // keyboard
  ad.setCompleteServices(NimBLEUUID(static_cast<uint16_t>(0x1812)));
  ad.setName("Desk Keyboard");
  adv->setAdvertisementData(ad);
  NimBLEAdvertisementData sr;
  sr.setCompleteServices(NimBLEUUID(kCtlSvc));
  adv->setScanResponseData(sr);
  adv->setMinInterval(32);  // 20 ms, so hosts reconnect quickly
  adv->setMaxInterval(48);
}

void setSlot(Slot s) {
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
  const bool room = server->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
  if (room && !adv->isAdvertising()) adv->start();
  if (!room && adv->isAdvertising()) adv->stop();
}

bool connected() { return hostLinked(); }

bool macConnected() {
  bool ok = false;
  portENTER_CRITICAL(&mux);
  for (auto &c : conns)
    if (c.used && c.authed && c.role == kMacRole) ok = true;
  portEXIT_CRITICAL(&mux);
  return ok;
}

const char *lastEvent() { return lastEventText; }

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

bool takeStep(int &dir) {
  const int s = pendingStep;
  if (s == 0) return false;
  pendingStep = 0;
  dir = s;
  return true;
}

void publishTarget(uint8_t target) {
  stateChr->setValue(&target, 1);
  stateChr->notify();
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
  const int role = (slot == Slot::None) ? kMacRole : static_cast<int>(slot);
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

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

const char *kPeerKey[2] = {"peerW", "peerG"};  // stored host address per slot

NimBLEServer *server = nullptr;
NimBLECharacteristic *input = nullptr;
Preferences prefs;

Slot slot = Slot::None;
bool haveConn = false;
bool authed = false;
bool linked = false;
uint16_t connHandle = 0;
std::string peerStr;

volatile bool policyDirty = false;
volatile bool changed = false;
volatile bool havePass = false;
volatile uint32_t pass = 0;

void setLinked(bool v) {
  if (linked != v) {
    linked = v;
    changed = true;
  }
}

void disconnectPeer() {
  if (haveConn && server) server->disconnect(connHandle);
}

// Decide whether the connected host is allowed for the current slot.
// The first host to pair while a slot is active becomes that slot's host.
void applyPolicy() {
  if (!haveConn || !authed) return;
  if (slot == Slot::None) {
    disconnectPeer();
    return;
  }
  const int me = static_cast<int>(slot);
  const int other = 1 - me;
  const String addr = peerStr.c_str();
  const String mine = prefs.getString(kPeerKey[me], "");
  const String theirs = prefs.getString(kPeerKey[other], "");
  if (mine.length() == 0) {
    if (addr == theirs) {  // that host belongs to the other slot
      disconnectPeer();
      return;
    }
    prefs.putString(kPeerKey[me], addr);
    setLinked(true);
  } else if (addr == mine) {
    setLinked(true);
  } else {
    disconnectPeer();
  }
}

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    connHandle = desc->conn_handle;
    haveConn = true;
    authed = false;
    peerStr = NimBLEAddress(desc->peer_id_addr).toString();
  }
  void onDisconnect(NimBLEServer *) override {
    haveConn = false;
    authed = false;
    linked = false;
    changed = true;  // also ends any pairing overlay
  }
  uint32_t onPassKeyRequest() override {
    pass = 100000 + (esp_random() % 900000);
    havePass = true;
    return pass;
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    if (!desc->sec_state.encrypted) {
      server->disconnect(desc->conn_handle);
      return;
    }
    peerStr = NimBLEAddress(desc->peer_id_addr).toString();
    authed = true;
    policyDirty = true;
  }
};

void sendReport(uint8_t mods, uint8_t key) {
  uint8_t r[8] = {mods, 0, key, 0, 0, 0, 0, 0};
  input->setValue(r, sizeof(r));
  input->notify();
}

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

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);  // keyboard
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
}

void setSlot(Slot s) {
  slot = s;
  setLinked(false);
  policyDirty = haveConn && authed;
}

void poll() {
  if (policyDirty) {
    policyDirty = false;
    applyPolicy();
  }
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  const bool shouldAdvertise = (slot != Slot::None) && !haveConn;
  if (shouldAdvertise && !adv->isAdvertising()) adv->start();
  if (!shouldAdvertise && adv->isAdvertising()) adv->stop();
}

bool connected() { return linked && haveConn; }

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
  if (!connected()) return;
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
  if (slot == Slot::None) return;
  const int me = static_cast<int>(slot);
  const String mine = prefs.getString(kPeerKey[me], "");
  if (mine.length() > 0) {
    for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; --i) {
      NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
      if (String(a.toString().c_str()) == mine) NimBLEDevice::deleteBond(a);
    }
    prefs.remove(kPeerKey[me]);
  }
  disconnectPeer();
  setLinked(false);
  changed = true;
}

}  // namespace kbd

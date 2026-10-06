import CoreBluetooth

/// Talks to the KVM device over Bluetooth LE.
/// The device exposes a control service that needs an encrypted, passkey-paired link:
/// macOS shows a pairing prompt the first time; enter the 6-digit code from the device screen.
final class BLEClient: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let service = CBUUID(string: "7D1B0001-5A3C-4F8E-9C1D-4B6A2E0F1A01")
    static let keysUUID = CBUUID(string: "7D1B0002-5A3C-4F8E-9C1D-4B6A2E0F1A01")
    static let cmdUUID = CBUUID(string: "7D1B0003-5A3C-4F8E-9C1D-4B6A2E0F1A01")
    static let stateUUID = CBUUID(string: "7D1B0004-5A3C-4F8E-9C1D-4B6A2E0F1A01")
    static let versionUUID = CBUUID(string: "7D1B0005-5A3C-4F8E-9C1D-4B6A2E0F1A01")
    static let deviceName = "Desk Keyboard"

    /// 0 = Mac, 1 = Work, 2 = Game. Called whenever the device reports its target.
    var onTarget: ((Int) -> Void)?
    /// Human-readable connection status, for the menu.
    var onStatus: ((String) -> Void)?
    /// Firmware version text reported by the device (older firmware does not report one).
    var onDeviceVersion: ((String) -> Void)?
    /// Called when readiness changes (true once paired, subscribed and the target is known).
    var onReady: ((Bool) -> Void)?

    private(set) var isReady = false {
        didSet { if oldValue != isReady { onReady?(isReady) } }
    }

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var keysChar: CBCharacteristic?
    private var cmdChar: CBCharacteristic?
    private var stateChar: CBCharacteristic?
    private var versionChar: CBCharacteristic?
    private let savedIDKey = "devicePeripheralID"

    func start() {
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: Sending

    /// Sends an 8-byte HID report (modifiers, reserved, 6 keys).
    func sendKeys(_ report: Data) {
        guard isReady, let p = peripheral, let c = keysChar else { return }
        p.writeValue(report, for: c, type: .withoutResponse)
    }

    /// Asks the device to move its target: -1 = left, +1 = right.
    func sendStep(_ dir: Int) {
        guard isReady, let p = peripheral, let c = cmdChar else { return }
        p.writeValue(Data([dir < 0 ? 1 : 2]), for: c, type: .withResponse)
    }

    // MARK: Scanning and connecting

    private func status(_ s: String) { onStatus?(s) }

    private func beginScan() {
        guard central.state == .poweredOn else { return }
        // Reconnect to the remembered device directly when possible.
        if let idString = UserDefaults.standard.string(forKey: savedIDKey),
           let id = UUID(uuidString: idString),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            connect(known)
            return
        }
        status("Scanning for the device…")
        central.scanForPeripherals(withServices: nil, options: nil)
    }

    private func connect(_ p: CBPeripheral) {
        central.stopScan()
        peripheral = p
        p.delegate = self
        status("Connecting…")
        central.connect(p, options: nil)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: beginScan()
        case .unauthorized: status("Bluetooth permission denied (System Settings > Privacy)")
        case .poweredOff: status("Bluetooth is off")
        default: status("Bluetooth unavailable")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name
        let services = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        if name == Self.deviceName || services.contains(Self.service) {
            connect(p)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        UserDefaults.standard.set(p.identifier.uuidString, forKey: savedIDKey)
        status("Connected, looking for services…")
        p.discoverServices([Self.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        reset(reason: "Connection failed, retrying…")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        reset(reason: "Disconnected, retrying…")
    }

    private func reset(reason: String) {
        isReady = false
        keysChar = nil
        cmdChar = nil
        stateChar = nil
        versionChar = nil
        status(reason)
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in self?.beginScan() }
    }

    // MARK: Services

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = p.services?.first(where: { $0.uuid == Self.service }) else {
            // The device may not have refreshed its service list; try again from scratch.
            status("Control service not found")
            central.cancelPeripheralConnection(p)
            return
        }
        p.discoverCharacteristics([Self.keysUUID, Self.cmdUUID, Self.stateUUID, Self.versionUUID], for: svc)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            switch c.uuid {
            case Self.keysUUID: keysChar = c
            case Self.cmdUUID: cmdChar = c
            case Self.stateUUID: stateChar = c
            case Self.versionUUID: versionChar = c
            default: break
            }
        }
        guard let state = stateChar, keysChar != nil, cmdChar != nil else {
            status("Control characteristics missing")
            return
        }
        // Reading and subscribing to the state needs an encrypted link, so this
        // is what triggers the pairing prompt the first time.
        status("Pairing: enter the code shown on the device")
        p.setNotifyValue(true, for: state)
        p.readValue(for: state)
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        if let error = error {
            status("Waiting for pairing (\(error.localizedDescription))")
            return
        }
        if c.uuid == Self.versionUUID {
            if let data = c.value, let text = String(data: data, encoding: .utf8) { onDeviceVersion?(text) }
            return
        }
        guard c.uuid == Self.stateUUID, let byte = c.value?.first else { return }
        let firstTime = !isReady
        isReady = true
        status("Connected")
        onTarget?(Int(byte))
        if firstTime, let v = versionChar { p.readValue(for: v) }
    }
}

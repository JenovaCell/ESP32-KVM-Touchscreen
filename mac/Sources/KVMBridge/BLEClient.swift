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
    /// Called whenever a diagnostic event is added to `events`.
    var onLog: (() -> Void)?

    private(set) var isReady = false {
        didSet { if oldValue != isReady { onReady?(isReady) } }
    }

    /// Recent Bluetooth events, oldest first, each stamped with the time. For diagnosis.
    private(set) var events: [String] = []

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var keysChar: CBCharacteristic?
    private var cmdChar: CBCharacteristic?
    private var stateChar: CBCharacteristic?
    private var versionChar: CBCharacteristic?
    private let savedIDKey = "devicePeripheralID"

    /// The most recent problem, kept so it stays visible while the app retries.
    private var lastIssue = ""
    private var connectWatchdog: DispatchWorkItem?

    private static let timeFormat: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()

    func start() {
        log("app started")
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: Diagnostics

    func log(_ text: String) {
        events.append("\(Self.timeFormat.string(from: Date())) \(text)")
        if events.count > 60 { events.removeFirst(events.count - 60) }
        onLog?()
    }

    /// Turns a Bluetooth error into plain text with its name and number.
    static func describe(_ error: Error?) -> String {
        guard let error = error else { return "no error" }
        let ns = error as NSError
        if let cb = error as? CBError { return "CBError \(ns.code) \(cb.code): \(ns.localizedDescription)" }
        if let att = error as? CBATTError { return "CBATTError \(ns.code) \(att.code): \(ns.localizedDescription)" }
        return "\(ns.domain) \(ns.code): \(ns.localizedDescription)"
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

    private func connectingText() -> String {
        lastIssue.isEmpty ? "Connecting…" : "Connecting… (last: \(lastIssue))"
    }

    private func beginScan() {
        guard central.state == .poweredOn else { return }
        // Reconnect to the remembered device directly when possible.
        if let idString = UserDefaults.standard.string(forKey: savedIDKey),
           let id = UUID(uuidString: idString),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            log("remembered device found, connecting directly")
            connect(known)
            return
        }
        log("scanning for the device")
        status(lastIssue.isEmpty ? "Scanning for the device…" : "Scanning… (last: \(lastIssue))")
        central.scanForPeripherals(withServices: nil, options: nil)
    }

    private func connect(_ p: CBPeripheral) {
        central.stopScan()
        peripheral = p
        p.delegate = self
        status(connectingText())
        log("connect() called")
        central.connect(p, options: nil)
        armWatchdog()
    }

    /// If the device does not accept the connection within 15 s, say so instead of sitting on "Connecting…".
    private func armWatchdog() {
        connectWatchdog?.cancel()
        let item = DispatchWorkItem { [weak self] in
            guard let self = self, let p = self.peripheral, p.state == .connecting else { return }
            self.lastIssue = "no response from the device after 15 s"
            self.log("still connecting after 15 s: the device is not accepting the connection")
            self.status(self.connectingText())
        }
        connectWatchdog = item
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: item)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            log("Bluetooth is on")
            beginScan()
        case .unauthorized:
            log("Bluetooth permission denied")
            status("Bluetooth permission denied (System Settings > Privacy)")
        case .poweredOff:
            log("Bluetooth is off")
            status("Bluetooth is off")
        default:
            log("Bluetooth state \(central.state.rawValue)")
            status("Bluetooth unavailable")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name
        let services = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        if name == Self.deviceName || services.contains(Self.service) {
            log("discovered \(name ?? "device"), signal \(RSSI)")
            connect(p)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        connectWatchdog?.cancel()
        UserDefaults.standard.set(p.identifier.uuidString, forKey: savedIDKey)
        log("connected (didConnect)")
        status("Connected, looking for services…")
        p.discoverServices([Self.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        connectWatchdog?.cancel()
        lastIssue = "connection failed: \(Self.describe(error))"
        log("didFailToConnect: \(Self.describe(error))")
        reset(reason: "Connection failed (\(Self.describe(error))), retrying…")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        connectWatchdog?.cancel()
        lastIssue = "disconnected: \(Self.describe(error))"
        log("didDisconnectPeripheral: \(Self.describe(error))")
        reset(reason: "Disconnected (\(Self.describe(error))), retrying…")
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
        let found = p.services?.map { $0.uuid.uuidString.prefix(8) }.joined(separator: ", ") ?? "none"
        log("services: \(found); \(Self.describe(error))")
        guard let svc = p.services?.first(where: { $0.uuid == Self.service }) else {
            // The device may not have refreshed its service list; try again from scratch.
            lastIssue = "control service not found"
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
        log("characteristics: keys \(keysChar != nil), cmd \(cmdChar != nil), state \(stateChar != nil), version \(versionChar != nil); \(Self.describe(error))")
        guard let state = stateChar, keysChar != nil, cmdChar != nil else {
            lastIssue = "control characteristics missing"
            status("Control characteristics missing")
            return
        }
        // Reading and subscribing to the state needs an encrypted link, so this
        // is what triggers the pairing prompt the first time.
        status("Pairing: enter the code shown on the device")
        log("subscribing and reading state (this triggers pairing)")
        p.setNotifyValue(true, for: state)
        p.readValue(for: state)
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        if let error = error {
            lastIssue = "subscribe failed: \(Self.describe(error))"
            log("subscribe failed: \(Self.describe(error))")
        } else {
            log("subscribed (notifying: \(c.isNotifying))")
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        if let error = error { log("write failed: \(Self.describe(error))") }
    }

    func peripheral(_ p: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        log("device changed its services")
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        if let error = error {
            lastIssue = "read failed: \(Self.describe(error))"
            log("read failed: \(Self.describe(error))")
            status("Waiting for pairing (\(Self.describe(error)))")
            return
        }
        if c.uuid == Self.versionUUID {
            if let data = c.value, let text = String(data: data, encoding: .utf8) {
                log("device firmware \(text)")
                onDeviceVersion?(text)
            }
            return
        }
        guard c.uuid == Self.stateUUID, let byte = c.value?.first else { return }
        let firstTime = !isReady
        isReady = true
        lastIssue = ""
        log("ready: device target \(byte)")
        status("Connected")
        onTarget?(Int(byte))
        if firstTime, let v = versionChar { p.readValue(for: v) }
    }
}

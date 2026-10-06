import Darwin
import Foundation

/// Talks to the KVM board over its USB cable.
///
/// The board shows up as a serial port (`/dev/cu.usbmodem...`). Messages are single text lines;
/// anything that does not start with "@" is the board's own debug text and is ignored.
///
///   Mac to board:  @H           heartbeat, once a second
///                  @K <16 hex>  one 8-byte keyboard report
///                  @S L | @S R  step the target left (toward GAME) or right (toward WORK)
///   Board to Mac:  @T <0|1|2>   active target (0 = Mac, 1 = Work, 2 = Game)
///                  @V <ver> <b> firmware version and build
final class DeviceLink {
    /// 0 = Mac, 1 = Work, 2 = Game. Called whenever the board reports its target.
    var onTarget: ((Int) -> Void)?
    /// Human-readable link status, for the menu.
    var onStatus: ((String) -> Void)?
    /// Firmware version text reported by the board.
    var onDeviceVersion: ((String) -> Void)?
    /// Called when readiness changes (true once the board has answered).
    var onReady: ((Bool) -> Void)?
    /// Called whenever a diagnostic event is added to `events`.
    var onLog: (() -> Void)?

    private(set) var isReady = false {
        didSet { if oldValue != isReady { onReady?(isReady) } }
    }

    /// Recent link events, oldest first, each stamped with the time. For diagnosis.
    private(set) var events: [String] = []

    private var fd: Int32 = -1
    private var currentPath = ""
    private var readSource: DispatchSourceRead?
    private var rxBuffer = [UInt8]()
    private var timer: Timer?
    private var lastHeard = Date.distantPast
    private var attempt = 0
    private var lastStatus = ""
    private let savedPathKey = "boardSerialPath"

    // Key report bookkeeping.
    private var lastSent: Data?
    private var resendItems: [DispatchWorkItem] = []
    private(set) var reportsSent = 0
    private(set) var releaseResends = 0
    private(set) var writeErrors = 0
    private(set) var linesFromBoard = 0

    private static let timeFormat: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()

    var statsText: String {
        "keys sent \(reportsSent), release re-sends \(releaseResends), write errors \(writeErrors), board lines \(linesFromBoard)"
    }

    func start() {
        log("app started")
        tick()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in self?.tick() }
    }

    // MARK: Diagnostics

    /// Millisecond-stamped trace of key events, reports sent and what the board relayed,
    /// to find where a key release goes wrong (KVM-20). Oldest first.
    private(set) var keyTrace: [String] = []
    private static let traceFormat: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()

    func trace(_ text: String) {
        keyTrace.append("\(Self.traceFormat.string(from: Date())) \(text)")
        if keyTrace.count > 120 { keyTrace.removeFirst(keyTrace.count - 120) }
    }

    func log(_ text: String) {
        events.append("\(Self.timeFormat.string(from: Date())) \(text)")
        if events.count > 60 { events.removeFirst(events.count - 60) }
        onLog?()
    }

    private func status(_ s: String) {
        if s != lastStatus {
            lastStatus = s
            onStatus?(s)
        }
    }

    // MARK: Sending

    /// Sends an 8-byte keyboard report (modifiers, reserved, 6 keys).
    func sendKeys(_ report: Data) {
        guard isReady else { return }
        let allUp = !report.contains { $0 != 0 }
        transmit(report, force: false)
        // Cheap insurance: after the last key is released, repeat "all keys up" shortly after,
        // in case the Bluetooth leg to the other computer lost one.
        cancelResends()
        if allUp { scheduleReleaseResends(report) }
    }

    /// Asks the board to move its target: -1 = left, +1 = right.
    func sendStep(_ dir: Int) {
        guard isReady else { return }
        send(dir < 0 ? "@S L\n" : "@S R\n")
    }

    private func transmit(_ report: Data, force: Bool) {
        if !force && report == lastSent { return }  // auto-repeat events add nothing new
        lastSent = report
        let hex = report.map { String(format: "%02x", $0) }.joined()
        trace((force ? "resend " : "send   ") + hex)
        send("@K \(hex)\n")
        reportsSent += 1
    }

    private func cancelResends() {
        resendItems.forEach { $0.cancel() }
        resendItems.removeAll()
    }

    private func scheduleReleaseResends(_ report: Data) {
        for delay in [0.04, 0.15] {
            let item = DispatchWorkItem { [weak self] in
                guard let self = self, self.isReady else { return }
                self.releaseResends += 1
                self.transmit(report, force: true)
            }
            resendItems.append(item)
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: item)
        }
    }

    private func send(_ text: String) {
        guard fd >= 0 else { return }
        let bytes = Array(text.utf8)
        let n = write(fd, bytes, bytes.count)
        if n != bytes.count { writeErrors += 1 }
    }

    // MARK: Finding and opening the board

    private func candidatePorts() -> [String] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
        var ports = names.filter { $0.hasPrefix("cu.usbmodem") }.sorted().map { "/dev/" + $0 }
        // Try the port that worked last time first.
        if let saved = UserDefaults.standard.string(forKey: savedPathKey), let i = ports.firstIndex(of: saved) {
            ports.remove(at: i)
            ports.insert(saved, at: 0)
        }
        return ports
    }

    /// Once a second: if connected, send a heartbeat and watch for silence; if not, try a port.
    private func tick() {
        if fd >= 0 {
            send("@H\n")
            if Date().timeIntervalSince(lastHeard) > 3.0 {
                lose(isReady ? "the board stopped answering" : "\(currentPath) did not answer (not the board?)")
            }
            return
        }
        let ports = candidatePorts()
        guard !ports.isEmpty else {
            status("Board not found: plug it into this Mac with the USB-C cable")
            return
        }
        let path = ports[attempt % ports.count]
        attempt += 1
        open(path)
    }

    private func open(_ path: String) {
        let f = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        if f < 0 {
            let reason = String(cString: strerror(errno))
            log("could not open \(path): \(reason)")
            status("Could not open \(path): \(reason)")
            return
        }
        var t = termios()
        if tcgetattr(f, &t) == 0 {
            cfmakeraw(&t)
            cfsetspeed(&t, speed_t(115200))
            t.c_cflag |= tcflag_t(CLOCAL | CREAD)
            tcsetattr(f, TCSANOW, &t)
        }
        fd = f
        currentPath = path
        rxBuffer.removeAll()
        lastHeard = Date()  // grace period for the board to answer
        log("opened \(path), waiting for the board to answer")
        status("Opened \(path), waiting for the board…")
        let source = DispatchSource.makeReadSource(fileDescriptor: f, queue: .main)
        source.setEventHandler { [weak self] in self?.readAvailable() }
        source.setCancelHandler { Darwin.close(f) }
        source.resume()
        readSource = source
        send("@H\n")
    }

    private func lose(_ reason: String) {
        log("link lost: \(reason)")
        readSource?.cancel()
        readSource = nil
        fd = -1
        currentPath = ""
        rxBuffer.removeAll()
        lastSent = nil
        cancelResends()
        isReady = false
        status("Lost the board (\(reason)), looking again…")
    }

    // MARK: Reading

    private func readAvailable() {
        guard fd >= 0 else { return }
        var buffer = [UInt8](repeating: 0, count: 512)
        let n = read(fd, &buffer, buffer.count)
        if n > 0 {
            rxBuffer.append(contentsOf: buffer[0..<n])
            parseLines()
        } else if n == 0 {
            lose("the port closed (cable unplugged?)")
        } else if errno != EAGAIN && errno != EINTR {
            lose("read error: \(String(cString: strerror(errno)))")
        }
    }

    private func parseLines() {
        while let newline = rxBuffer.firstIndex(of: 0x0A) {
            let lineBytes = Array(rxBuffer[0..<newline])
            rxBuffer.removeSubrange(0...newline)
            let text = String(decoding: lineBytes, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
            handle(text)
        }
        if rxBuffer.count > 4096 { rxBuffer.removeAll() }  // never grow without bound on noise
    }

    private func handle(_ line: String) {
        guard line.hasPrefix("@") else { return }  // the board's own debug text
        lastHeard = Date()
        linesFromBoard += 1
        let parts = line.split(separator: " ", maxSplits: 1).map(String.init)
        switch parts[0] {
        case "@T":
            guard parts.count == 2, let target = Int(parts[1]) else { return }
            if !isReady {
                UserDefaults.standard.set(currentPath, forKey: savedPathKey)
                log("board answered on \(currentPath): ready, target \(target)")
                isReady = true
                status("Connected (USB)")
            }
            onTarget?(target)
        case "@R":  // "@R <board ms> <+|-> <hex>": what the board relayed to the PC
            trace("board  " + (parts.count == 2 ? parts[1] : ""))
        case "@V":
            if parts.count == 2 { onDeviceVersion?(parts[1]) }
        default:
            break
        }
    }
}

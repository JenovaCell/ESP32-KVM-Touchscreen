import Cocoa
import CoreGraphics

/// Captures the Mac keyboard. In Mac mode keys pass through untouched. In Work or Game mode
/// they are swallowed and sent to the device as HID reports. A double-tap of the left or
/// right Command key switches the device's target (left = toward Game, right = toward Work).
final class KeyBridge {
    private let ble: DeviceLink
    private var tap: CFMachPort?
    private var retryTimer: Timer?

    /// 0 = Mac, 1 = Work, 2 = Game.
    private(set) var target = 0
    /// Only trap the keyboard when the device link is up, so it can never lock you out.
    private var swallowing: Bool { target != 0 && ble.isReady }
    private(set) var tapActive = false
    var onTapStatus: ((Bool) -> Void)?

    // HID report state while swallowing.
    private var mods: UInt8 = 0
    private var pressed: [UInt8] = []

    // Mouse (KVM-9): while the target is not the Mac the pointer is frozen on the Mac and its movement,
    // clicks and scrolling go to the PC.
    private static let mouseTypes: Set<CGEventType> = [
        .mouseMoved, .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp,
        .leftMouseDragged, .rightMouseDragged, .otherMouseDown, .otherMouseUp, .otherMouseDragged, .scrollWheel,
    ]
    private var capturing = false
    private var frozenPoint = CGPoint.zero  // where the Mac pointer was frozen
    private var lastReassert = Date.distantPast
    private var cursorHidden = false
    private var mouseButtons = 0
    private var sentMouseButtons = 0
    private var accX = 0.0, accY = 0.0, accWheel = 0.0, accPan = 0.0
    private var flushTimer: Timer?
    /// Pointer speed multiplier (menu setting, remembered).
    var pointerScale: Double = {
        let v = UserDefaults.standard.double(forKey: "pointerScale")
        return v > 0 ? v : 1.0
    }() {
        didSet { UserDefaults.standard.set(pointerScale, forKey: "pointerScale") }
    }
    /// Per-machine mouse switches (menu settings, remembered). The Mac always has its mouse.
    var mouseOnWork: Bool = (UserDefaults.standard.object(forKey: "mouseOnWork") as? Bool) ?? true {
        didSet {
            UserDefaults.standard.set(mouseOnWork, forKey: "mouseOnWork")
            updateCapture()
        }
    }
    var mouseOnGame: Bool = (UserDefaults.standard.object(forKey: "mouseOnGame") as? Bool) ?? true {
        didSet {
            UserDefaults.standard.set(mouseOnGame, forKey: "mouseOnGame")
            updateCapture()
        }
    }
    /// True while the pointer belongs to the PC of the current target (target is Work or Game, and its switch is on).
    private var mouseActive: Bool {
        guard swallowing else { return false }
        switch target {
        case 1: return mouseOnWork
        case 2: return mouseOnGame
        default: return false
        }
    }
    /// Flips the scroll direction sent to the PC (menu setting, remembered).
    var invertScroll: Bool = UserDefaults.standard.bool(forKey: "invertScroll") {
        didSet { UserDefaults.standard.set(invertScroll, forKey: "invertScroll") }
    }
    private var swallowedCodes = Set<Int>()
    private var modState: UInt8 = 0          // modifiers the Mac currently sees as down
    private var modSwallowed: UInt8 = 0      // of those, the ones whose press we swallowed

    // Command double-tap tracking: index 0 = left, 1 = right.
    private var cmdDown = [false, false]
    private var cmdSwallowedDown = [false, false]
    private var cmdDownAt = [Date.distantPast, Date.distantPast]
    private var cmdCommitted = [false, false]
    private var lastTap: [Date?] = [nil, nil]
    private var pendingTap: [DispatchWorkItem?] = [nil, nil]
    private let cmdBit: [UInt8] = [0x08, 0x80]
    private let tapMax: TimeInterval = 0.30      // longest press that counts as a tap
    private let doubleWindow: TimeInterval = 0.35

    init(ble: DeviceLink) { self.ble = ble }

    // MARK: Event tap

    func start() {
        if tap != nil { return }
        let mask: CGEventMask =
            (1 << CGEventType.keyDown.rawValue) |
            (1 << CGEventType.keyUp.rawValue) |
            (1 << CGEventType.flagsChanged.rawValue) |
            (1 << 14) |  // system-defined events: volume, brightness, play/pause and the other function keys
            KeyBridge.mouseTypes.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << $1.rawValue) }
        let refcon = Unmanaged.passUnretained(self).toOpaque()
        let callback: CGEventTapCallBack = { _, type, event, refcon in
            guard let refcon = refcon else { return Unmanaged.passUnretained(event) }
            let me = Unmanaged<KeyBridge>.fromOpaque(refcon).takeUnretainedValue()
            return me.handle(type: type, event: event)
        }
        guard let t = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap,
                                        options: .defaultTap, eventsOfInterest: mask,
                                        callback: callback, userInfo: refcon) else {
            // No permission yet; keep retrying until the user grants it.
            setTapActive(false)
            retryTimer?.invalidate()
            retryTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
                self?.start()
            }
            return
        }
        retryTimer?.invalidate()
        retryTimer = nil
        tap = t
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, t, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: t, enable: true)
        setTapActive(true)
    }

    private func setTapActive(_ v: Bool) {
        if tapActive != v {
            tapActive = v
            onTapStatus?(v)
        }
    }

    fileprivate func handle(type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let t = tap { CGEvent.tapEnable(tap: t, enable: true) }
            return Unmanaged.passUnretained(event)
        }
        if type.rawValue == 14 { return handleSystem(event) }
        if Self.mouseTypes.contains(type) { return handleMouse(type: type, event: event) }
        let code = Int(event.getIntegerValueField(.keyboardEventKeycode))
        switch type {
        case .flagsChanged: return handleFlags(code: code, event: event)
        case .keyDown: return handleKey(code: code, down: true, event: event)
        case .keyUp: return handleKey(code: code, down: false, event: event)
        default: return Unmanaged.passUnretained(event)
        }
    }

    // MARK: Media keys (KVM-6)

    /// Mac media key number (NX_KEYTYPE_*) to HID consumer usage.
    private static let consumerUsage: [Int: UInt16] = [
        0: 0xE9,   // volume up
        1: 0xEA,   // volume down
        2: 0x6F,   // brightness up
        3: 0x70,   // brightness down
        7: 0xE2,   // mute
        16: 0xCD,  // play / pause
        17: 0xB5,  // next track
        18: 0xB6,  // previous track
        19: 0xB5,  // fast forward = next track
        20: 0xB6,  // rewind = previous track
    ]
    private var consumerDown: UInt16 = 0

    private func handleSystem(_ event: CGEvent) -> Unmanaged<CGEvent>? {
        let pass = Unmanaged.passUnretained(event)
        guard swallowing, let ns = NSEvent(cgEvent: event), ns.subtype.rawValue == 8 else { return pass }
        let keyType = (ns.data1 & 0xFFFF0000) >> 16
        let flags = ns.data1 & 0x0000FFFF
        let isDown = ((flags & 0xFF00) >> 8) == 0xA
        guard let usage = Self.consumerUsage[keyType] else { return pass }  // e.g. keyboard backlight: stays on the Mac
        if isDown {
            if consumerDown != usage {
                consumerDown = usage
                ble.sendConsumer(usage)
            }
        } else if consumerDown == usage {
            consumerDown = 0
            ble.sendConsumer(0)
        }
        return nil
    }

    // MARK: Mouse (KVM-9)

    private func handleMouse(type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        let pass = Unmanaged.passUnretained(event)
        // A button that went down on the Mac before capture started goes up on the Mac too.
        func button(_ bit: Int, down: Bool) -> Unmanaged<CGEvent>? {
            if down {
                guard mouseActive else { return pass }
                mouseButtons |= bit
            } else {
                guard mouseButtons & bit != 0 else { return pass }
                mouseButtons &= ~bit
            }
            flushMouse()
            return nil
        }
        switch type {
        case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged:
            guard mouseActive else { return pass }
            accX += Double(event.getIntegerValueField(.mouseEventDeltaX))
            accY += Double(event.getIntegerValueField(.mouseEventDeltaY))
            // Some apps (Elgato Studio) link the pointer to the mouse again. If the Mac pointer drifted
            // from where it froze, put it back (KVM-29).
            let here = event.location
            if abs(here.x - frozenPoint.x) > 1 || abs(here.y - frozenPoint.y) > 1 {
                CGWarpMouseCursorPosition(frozenPoint)
            }
            return nil
        case .leftMouseDown: return button(1, down: true)
        case .leftMouseUp: return button(1, down: false)
        case .rightMouseDown: return button(2, down: true)
        case .rightMouseUp: return button(2, down: false)
        case .otherMouseDown, .otherMouseUp:
            guard event.getIntegerValueField(.mouseEventButtonNumber) == 2 else { return mouseActive ? nil : pass }
            return button(4, down: type == .otherMouseDown)
        case .scrollWheel:
            guard mouseActive else { return pass }
            let sign = invertScroll ? -1.0 : 1.0
            accWheel += sign * Double(event.getIntegerValueField(.scrollWheelEventDeltaAxis1))
            accPan += sign * Double(event.getIntegerValueField(.scrollWheelEventDeltaAxis2))
            return nil
        default:
            return pass
        }
    }

    /// Sends the movement collected since the last call (called every 10 ms while capturing, and on clicks).
    private func flushMouse() {
        // Keep the freeze in place even if an app undid it (checked a few times a second).
        if capturing, Date().timeIntervalSince(lastReassert) > 0.25 {
            lastReassert = Date()
            _ = CGAssociateMouseAndMouseCursorPosition(0)
        }
        let dx = Int((accX * pointerScale).rounded()), dy = Int((accY * pointerScale).rounded())
        let w = Int(accWheel.rounded()), p = Int(accPan.rounded())
        guard dx != 0 || dy != 0 || w != 0 || p != 0 || mouseButtons != sentMouseButtons else { return }
        accX -= Double(dx) / pointerScale
        accY -= Double(dy) / pointerScale
        accWheel -= Double(w)
        accPan -= Double(p)
        sentMouseButtons = mouseButtons
        ble.sendMouse(buttons: mouseButtons, dx: dx, dy: dy, wheel: w, pan: p)
    }

    /// Freezes (and hides) the Mac pointer while the PC has the mouse; gives it back otherwise.
    private func updateCapture() {
        let want = mouseActive
        if want == capturing { return }
        capturing = want
        if want {
            frozenPoint = CGEvent(source: nil)?.location ?? .zero
            CGSetLocalEventsSuppressionInterval(0)  // moving the pointer back must not pause the mouse
            _ = CGAssociateMouseAndMouseCursorPosition(0)
            CGDisplayHideCursor(CGMainDisplayID())
            cursorHidden = true
            flushTimer = Timer.scheduledTimer(withTimeInterval: 0.010, repeats: true) { [weak self] _ in
                self?.flushMouse()
            }
        } else {
            flushTimer?.invalidate()
            flushTimer = nil
            if mouseButtons != 0 || sentMouseButtons != 0 {
                ble.sendMouse(buttons: 0, dx: 0, dy: 0, wheel: 0, pan: 0)  // let go of every button on the PC
            }
            mouseButtons = 0
            sentMouseButtons = 0
            accX = 0; accY = 0; accWheel = 0; accPan = 0
            restorePointer()
        }
    }

    private func restorePointer() {
        _ = CGAssociateMouseAndMouseCursorPosition(1)
        if cursorHidden {
            CGDisplayShowCursor(CGMainDisplayID())
            cursorHidden = false
        }
    }

    /// Called when the app quits, so the Mac pointer is never left frozen.
    func shutdown() {
        flushTimer?.invalidate()
        flushTimer = nil
        capturing = false
        restorePointer()
    }

    // MARK: Target changes

    func setTarget(_ t: Int) {
        let wasSwallowing = swallowing
        let oldTarget = target
        target = t
        // The board repeats its target every second (answer to the heartbeat). Only release the
        // keys when something really changed, or a held key / Ctrl+C is cut off mid-press (KVM-20, KVM-21).
        let changed = t != oldTarget || wasSwallowing != swallowing
        if changed && (wasSwallowing || swallowing) { releaseAll() }
        updateCapture()
    }

    /// Release every key on the host (used when switching target or losing the link).
    func releaseAll() {
        if consumerDown != 0 {
            consumerDown = 0
            ble.sendConsumer(0)
        }
        pressed.removeAll()
        mods = 0
        for s in 0..<2 {
            pendingTap[s]?.cancel()
            pendingTap[s] = nil
            lastTap[s] = nil
        }
        sendReport()
    }

    // MARK: Keys

    private func handleKey(code: Int, down: Bool, event: CGEvent) -> Unmanaged<CGEvent>? {
        let pass = Unmanaged.passUnretained(event)
        if down {
            // A key press cancels a Command double-tap and makes held Command keys real modifiers.
            for s in 0..<2 {
                lastTap[s] = nil
                if cmdDown[s] && !cmdCommitted[s] {
                    cmdCommitted[s] = true
                    if cmdSwallowedDown[s] { flushPendingTap(s); mods |= cmdBit[s] }
                }
            }
            guard swallowing else { return pass }
            swallowedCodes.insert(code)
            let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0
            ble.trace("key down \(code)\(isRepeat ? " (auto-repeat)" : "")")
            if !isRepeat, let usage = HIDMap.usage[code], !pressed.contains(usage), pressed.count < 6 {
                pressed.append(usage)
            }
            sendReport()
            return nil
        } else {
            if swallowedCodes.remove(code) != nil {
                ble.trace("key up   \(code)")
                if let usage = HIDMap.usage[code] { pressed.removeAll { $0 == usage } }
                sendReport()
                return nil
            }
            return pass
        }
    }

    private func handleFlags(code: Int, event: CGEvent) -> Unmanaged<CGEvent>? {
        let pass = Unmanaged.passUnretained(event)
        let flags = event.flags

        if code == HIDMap.capsLock {
            guard swallowing else { return pass }
            // Caps Lock arrives as a single toggle event: forward it as a tap.
            var d = Data([mods, 0, HIDMap.capsLockUsage, 0, 0, 0, 0, 0])
            ble.sendKeys(d)
            d = Data([mods, 0, 0, 0, 0, 0, 0, 0])
            ble.sendKeys(d)
            return nil
        }

        if code == 0x37 || code == 0x36 {
            return handleCommand(side: code == 0x37 ? 0 : 1, flags: flags, event: event)
        }

        guard let bit = HIDMap.modBit[code], let mask = HIDMap.modMask[code] else { return pass }
        let isDown = flags.contains(mask) && (modState & bit) == 0
        if isDown {
            modState |= bit
            guard swallowing else { return pass }
            modSwallowed |= bit
            mods |= bit
            sendReport()
            return nil
        } else {
            modState &= ~bit
            let wasSwallowed = (modSwallowed & bit) != 0
            modSwallowed &= ~bit
            if wasSwallowed {
                mods &= ~bit
                sendReport()
                return nil
            }
            return pass
        }
    }

    private func handleCommand(side s: Int, flags: CGEventFlags, event: CGEvent) -> Unmanaged<CGEvent>? {
        let pass = Unmanaged.passUnretained(event)
        let now = Date()
        let isDown = flags.contains(.maskCommand) && !cmdDown[s]

        if isDown {
            cmdDown[s] = true
            cmdDownAt[s] = now
            cmdCommitted[s] = false
            cmdSwallowedDown[s] = swallowing
            return swallowing ? nil : pass
        }

        // Release.
        cmdDown[s] = false
        let wasSwallowed = cmdSwallowedDown[s]
        let held = now.timeIntervalSince(cmdDownAt[s])

        if cmdCommitted[s] {
            if wasSwallowed { mods &= ~cmdBit[s]; sendReport() }
            lastTap[s] = nil
        } else if held < tapMax {
            if let prev = lastTap[s], now.timeIntervalSince(prev) < doubleWindow {
                // Double-tap: switch target. Drop the first tap's pending Windows-key press.
                lastTap[s] = nil
                pendingTap[s]?.cancel()
                pendingTap[s] = nil
                ble.sendStep(s == 0 ? -1 : +1)
            } else {
                lastTap[s] = now
                if wasSwallowed { scheduleHostTap(s) }
            }
        } else {
            // Long lone press: forward it as a Windows-key tap.
            lastTap[s] = nil
            if wasSwallowed { sendGuiTap(s) }
        }
        return wasSwallowed ? nil : pass
    }

    // MARK: Reports

    private func sendReport() {
        var d = Data([mods, 0])
        for i in 0..<6 { d.append(i < pressed.count ? pressed[i] : 0) }
        ble.sendKeys(d)
    }

    private func sendGuiTap(_ s: Int) {
        mods |= cmdBit[s]
        sendReport()
        mods &= ~cmdBit[s]
        sendReport()
    }

    /// A lone Command tap in Work/Game mode is only forwarded after the double-tap window
    /// passes, so a double-tap switches targets without poking the host.
    private func scheduleHostTap(_ s: Int) {
        pendingTap[s]?.cancel()
        let item = DispatchWorkItem { [weak self] in
            guard let self = self else { return }
            self.pendingTap[s] = nil
            self.lastTap[s] = nil
            if self.swallowing { self.sendGuiTap(s) }
        }
        pendingTap[s] = item
        DispatchQueue.main.asyncAfter(deadline: .now() + doubleWindow, execute: item)
    }

    private func flushPendingTap(_ s: Int) {
        if let item = pendingTap[s] {
            item.cancel()
            pendingTap[s] = nil
            sendGuiTap(s)
        }
    }
}

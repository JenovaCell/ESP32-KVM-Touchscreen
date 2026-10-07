import AppKit

/// Picks the target from the frontmost application (KVM-24). Only acts when the frontmost app
/// changes, so a manual switch stays until you move to another app.
final class AutoSwitch {
    /// Names are matched without case, anywhere in the app's name. Edit here to change the apps.
    static let workApps = ["elgato studio", "elegato studio"]
    static let gameApps = ["moonlight"]

    private let ble: DeviceLink
    private let defaultsKey = "autoSwitchByApp"
    private var observer: NSObjectProtocol?

    private(set) var enabled: Bool
    /// The target the board is on right now (0 Mac, 1 Work, 2 Game); set by the app.
    var currentTarget: () -> Int = { 0 }

    init(ble: DeviceLink) {
        self.ble = ble
        self.enabled = UserDefaults.standard.bool(forKey: defaultsKey)  // off until switched on
        observer = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification, object: nil, queue: .main
        ) { [weak self] note in
            let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
            self?.apply(app)
        }
        // A desktop (Space) change, such as a three-finger swipe or Mission Control, counts too (KVM-9).
        NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.activeSpaceDidChangeNotification, object: nil, queue: .main
        ) { [weak self] _ in
            // The frontmost app settles a moment after the desktop changes.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
                self?.apply(NSWorkspace.shared.frontmostApplication)
            }
        }
    }

    func setEnabled(_ on: Bool) {
        enabled = on
        UserDefaults.standard.set(on, forKey: defaultsKey)
        ble.log("auto-switch by app: \(on ? "on" : "off")")
        if on { apply(NSWorkspace.shared.frontmostApplication) }
    }

    static func target(forAppNamed name: String?) -> Int {
        let n = (name ?? "").lowercased()
        if workApps.contains(where: { n.contains($0) }) { return 1 }
        if gameApps.contains(where: { n.contains($0) }) { return 2 }
        return 0
    }

    private func apply(_ app: NSRunningApplication?) {
        guard ble.isReady, let app = app else { return }
        if app.bundleIdentifier == Bundle.main.bundleIdentifier { return }  // our own menu
        if !enabled {
            // Auto-switch is off: a desktop or app change still gives keyboard and pointer back to the Mac.
            if currentTarget() != 0 {
                ble.log("desktop or app changed (\(app.localizedName ?? "?")): back to Mac")
                ble.sendGoto(0)
            }
            return
        }
        let t = Self.target(forAppNamed: app.localizedName)
        ble.log("auto-switch: \(app.localizedName ?? "?") -> target \(t)")
        ble.sendGoto(t)
    }
}

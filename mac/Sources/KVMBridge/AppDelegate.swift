import AppKit
import ApplicationServices
import CoreGraphics

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let ble = DeviceLink()
    private lazy var keys = KeyBridge(ble: ble)

    private let targetNames = ["MAC", "WORK", "GAME"]
    private var target = 0
    private let appVersion =
        (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String) ?? "dev"
    private let versionItem = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    private let deviceVersionItem = NSMenuItem(title: "Device firmware: not connected", action: nil, keyEquivalent: "")
    private let bleItem = NSMenuItem(title: "Board link: starting…", action: nil, keyEquivalent: "")
    private let permItem = NSMenuItem(title: "Keyboard access: waiting", action: nil, keyEquivalent: "")
    private let statsItem = NSMenuItem(title: "Keys sent: 0", action: nil, keyEquivalent: "")
    private let eventsItem = NSMenuItem(title: "Recent events", action: nil, keyEquivalent: "")
    private let eventsMenu = NSMenu()

    func applicationDidFinishLaunching(_ notification: Notification) {
        versionItem.title = "KVMBridge v\(appVersion)"
        buildMenu()
        refreshTitle()

        ble.onLog = { [weak self] in self?.refreshEvents() }
        ble.onStatus = { [weak self] s in
            self?.bleItem.title = "Board link: \(s)"
        }
        ble.onDeviceVersion = { [weak self] v in
            self?.deviceVersionItem.title = "Device firmware: v\(v)"
        }
        ble.onTarget = { [weak self] t in
            self?.target = t
            self?.keys.setTarget(t)
            self?.refreshTitle()
        }
        ble.onReady = { [weak self] ready in
            guard let self = self else { return }
            if !ready {
                // Link lost: hand the keyboard back to the Mac.
                self.keys.setTarget(0)
            } else {
                self.keys.setTarget(self.target)
            }
            self.refreshTitle()
        }
        keys.onTapStatus = { [weak self] ok in
            self?.ble.log("keyboard access: \(ok ? "granted" : "waiting")")
            self?.permItem.title = ok ? "Keyboard access: granted"
                                      : "Keyboard access: grant Accessibility + Input Monitoring"
        }

        refreshEvents()
        requestPermissions()
        keys.start()
        ble.start()
    }

    private func requestPermissions() {
        let options = ["AXTrustedCheckOptionPrompt": true] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
        _ = CGRequestListenEventAccess()
    }

    private func buildMenu() {
        let menu = NSMenu()
        menu.addItem(versionItem)
        menu.addItem(deviceVersionItem)
        menu.addItem(.separator())
        menu.addItem(bleItem)
        menu.addItem(permItem)
        menu.addItem(statsItem)
        eventsItem.submenu = eventsMenu
        menu.addItem(eventsItem)
        let copy = NSMenuItem(title: "Copy diagnostics", action: #selector(copyDiagnostics), keyEquivalent: "")
        copy.target = self
        menu.addItem(copy)
        menu.addItem(.separator())
        let hint = NSMenuItem(title: "Double-tap Left Cmd: toward GAME", action: nil, keyEquivalent: "")
        let hint2 = NSMenuItem(title: "Double-tap Right Cmd: toward WORK", action: nil, keyEquivalent: "")
        menu.addItem(hint)
        menu.addItem(hint2)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Quit", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        menu.delegate = self
        statusItem.menu = menu
    }

    /// Refresh the key counters each time the menu is opened.
    func menuWillOpen(_ menu: NSMenu) {
        statsItem.title = ble.statsText
    }

    /// Shows the newest Bluetooth events in the "Recent events" submenu.
    private func refreshEvents() {
        eventsMenu.removeAllItems()
        let recent = ble.events.suffix(12).reversed()
        if recent.isEmpty {
            eventsMenu.addItem(NSMenuItem(title: "(none yet)", action: nil, keyEquivalent: ""))
            return
        }
        for line in recent { eventsMenu.addItem(NSMenuItem(title: line, action: nil, keyEquivalent: "")) }
    }

    /// Puts the full event history on the clipboard so it can be pasted into a ticket.
    @objc private func copyDiagnostics() {
        var lines = [
            "KVMBridge v\(appVersion)",
            deviceVersionItem.title,
            bleItem.title,
            permItem.title,
            ble.statsText,
            "macOS \(ProcessInfo.processInfo.operatingSystemVersionString)",
            "",
        ]
        lines.append(contentsOf: ble.events)
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(lines.joined(separator: "\n"), forType: .string)
    }

    private func refreshTitle() {
        let name = targetNames.indices.contains(target) ? targetNames[target] : "?"
        statusItem.button?.title = ble.isReady ? "⌨︎ \(name)" : "⌨︎ …"
    }
}

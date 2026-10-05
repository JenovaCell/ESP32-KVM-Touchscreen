import AppKit
import ApplicationServices
import CoreGraphics

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let ble = BLEClient()
    private lazy var keys = KeyBridge(ble: ble)

    private let targetNames = ["MAC", "WORK", "GAME"]
    private var target = 0
    private let bleItem = NSMenuItem(title: "Bluetooth: starting…", action: nil, keyEquivalent: "")
    private let permItem = NSMenuItem(title: "Keyboard access: waiting", action: nil, keyEquivalent: "")

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMenu()
        refreshTitle()

        ble.onStatus = { [weak self] s in
            self?.bleItem.title = "Bluetooth: \(s)"
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
            self?.permItem.title = ok ? "Keyboard access: granted"
                                      : "Keyboard access: grant Accessibility + Input Monitoring"
        }

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
        menu.addItem(bleItem)
        menu.addItem(permItem)
        menu.addItem(.separator())
        let hint = NSMenuItem(title: "Double-tap Left Cmd: toward GAME", action: nil, keyEquivalent: "")
        let hint2 = NSMenuItem(title: "Double-tap Right Cmd: toward WORK", action: nil, keyEquivalent: "")
        menu.addItem(hint)
        menu.addItem(hint2)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Quit", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        statusItem.menu = menu
    }

    private func refreshTitle() {
        let name = targetNames.indices.contains(target) ? targetNames[target] : "?"
        statusItem.button?.title = ble.isReady ? "⌨︎ \(name)" : "⌨︎ …"
    }
}

// swift-tools-version:5.7
import PackageDescription

let package = Package(
    name: "KVMBridge",
    platforms: [.macOS(.v12)],
    targets: [
        .executableTarget(name: "KVMBridge", path: "Sources/KVMBridge")
    ]
)

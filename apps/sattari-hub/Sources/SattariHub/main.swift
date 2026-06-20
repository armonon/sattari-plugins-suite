import AppKit
import Foundation

struct HubManifest: Codable {
    let schemaVersion: Int
    let suiteName: String
    let suiteVersion: String
    let truthLabel: String
    let defaultUpdateChannel: String
    let updateFeedURL: String?
    let products: [HubProduct]
}

struct HubProduct: Codable {
    let id: String
    let name: String
    let category: String
    let accent: String?            // shared brand accent ("#rrggbb"), from SattariBranding.h
    let version: String
    let status: String
    let description: String
    let formats: [String]
    let bundles: [InstallBundle]
    let updateArchiveURL: String?
}

struct InstallBundle: Codable {
    let kind: String
    let relativeSourcePath: String
    let installSubpath: String
}

struct InstallReceipt: Codable {
    let productId: String
    let version: String
    let installedAt: String
    let bundleCount: Int
    let updateChannel: String
}

final class HubModel {
    let manifest: HubManifest
    let repoRoot: URL?
    private let fileManager = FileManager.default
    private let appSupport: URL
    private let receiptsURL: URL
    private(set) var receipts: [String: InstallReceipt]

    init() throws {
        manifest = try Self.loadManifest()
        repoRoot = Self.resolveRepoRoot()
        appSupport = fileManager.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Sattari Hub", isDirectory: true)
        receiptsURL = appSupport.appendingPathComponent("receipts.json")
        try fileManager.createDirectory(at: appSupport, withIntermediateDirectories: true)
        receipts = Self.loadReceipts(from: receiptsURL)
    }

    static func loadManifest() throws -> HubManifest {
        let decoder = JSONDecoder()
        let candidates: [URL?] = [
            Bundle.main.resourceURL?.appendingPathComponent("sattari-products.json"),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                .appendingPathComponent("Sources/SattariHub/Resources/sattari-products.json"),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                .appendingPathComponent("apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json")
        ]

        var bundledManifest: HubManifest?
        for candidate in candidates.compactMap({ $0 }) where FileManager.default.fileExists(atPath: candidate.path) {
            let data = try Data(contentsOf: candidate)
            bundledManifest = try decoder.decode(HubManifest.self, from: data)
            break
        }

        guard let bundledManifest else {
            throw NSError(domain: "SattariHub", code: 1, userInfo: [NSLocalizedDescriptionKey: "Missing sattari-products.json"])
        }

        if ProcessInfo.processInfo.environment["SATTARI_HUB_OFFLINE"] == "1" {
            return bundledManifest
        }

        let feedURLString = ProcessInfo.processInfo.environment["SATTARI_HUB_FEED_URL"] ?? bundledManifest.updateFeedURL
        if let feedURLString,
           let feedURL = URL(string: feedURLString),
           let data = try? Data(contentsOf: feedURL),
           let remoteManifest = try? decoder.decode(HubManifest.self, from: data),
           remoteManifest.schemaVersion == bundledManifest.schemaVersion,
           remoteManifest.products.count >= bundledManifest.products.count {
            return remoteManifest
        }

        return bundledManifest
    }

    static func resolveRepoRoot() -> URL? {
        if let explicit = ProcessInfo.processInfo.environment["SATTARI_REPO_ROOT"], !explicit.isEmpty {
            let url = URL(fileURLWithPath: explicit, isDirectory: true)
            if FileManager.default.fileExists(atPath: url.appendingPathComponent("plugins").path) {
                return url
            }
        }

        var cursor = URL(fileURLWithPath: FileManager.default.currentDirectoryPath, isDirectory: true)
        for _ in 0..<8 {
            if FileManager.default.fileExists(atPath: cursor.appendingPathComponent("plugins/auto-pitch").path)
                && FileManager.default.fileExists(atPath: cursor.appendingPathComponent("scripts/package_alpha_local.sh").path) {
                return cursor
            }
            cursor.deleteLastPathComponent()
        }
        return nil
    }

    static func loadReceipts(from url: URL) -> [String: InstallReceipt] {
        guard let data = try? Data(contentsOf: url) else { return [:] }
        return (try? JSONDecoder().decode([String: InstallReceipt].self, from: data)) ?? [:]
    }

    private func remotePayloadCandidateRoots() -> [URL] {
        let remoteRoot = appSupport.appendingPathComponent("RemotePayloads", isDirectory: true)
        guard let children = try? fileManager.contentsOfDirectory(at: remoteRoot, includingPropertiesForKeys: nil) else { return [] }
        return children.filter { $0.hasDirectoryPath }
    }

    func sourceURL(for bundle: InstallBundle) -> URL? {
        var candidates: [URL?] = [
            repoRoot?.appendingPathComponent(bundle.relativeSourcePath),
            Bundle.main.resourceURL?.appendingPathComponent("Payload").appendingPathComponent(bundle.relativeSourcePath),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                .appendingPathComponent("Payload")
                .appendingPathComponent(bundle.relativeSourcePath)
        ]

        for root in remotePayloadCandidateRoots() {
            candidates.append(root.appendingPathComponent("Payload").appendingPathComponent(bundle.relativeSourcePath))
            candidates.append(root.appendingPathComponent(bundle.relativeSourcePath))
        }

        return candidates.compactMap { $0 }.first { fileManager.fileExists(atPath: $0.path) }
    }

    func destinationURL(for bundle: InstallBundle) -> URL {
        fileManager.homeDirectoryForCurrentUser.appendingPathComponent(bundle.installSubpath)
    }

    func productIsAvailable(_ product: HubProduct) -> Bool {
        product.bundles.contains { bundle in
            guard let source = sourceURL(for: bundle) else { return false }
            return fileManager.fileExists(atPath: source.path)
        }
    }

    func productCanDownloadUpdate(_ product: HubProduct) -> Bool {
        product.updateArchiveURL != nil
    }

    func productNeedsUpdate(_ product: HubProduct) -> Bool {
        receipts[product.id]?.version != product.version
    }

    func statusText(for product: HubProduct) -> String {
        let receipt = receipts[product.id]
        let availability = productIsAvailable(product) ? "package found" : (productCanDownloadUpdate(product) ? "online update available" : "package missing")
        if let receipt {
            return "Installed \(receipt.version) • \(productNeedsUpdate(product) ? "update available" : "current") • \(availability)"
        }
        return "Not installed • \(availability)"
    }

    func install(product: HubProduct) throws -> String {
        if !product.bundles.contains(where: { sourceURL(for: $0) != nil }) {
            try prepareRemotePayloadIfAvailable(for: product)
        }

        guard product.bundles.contains(where: { sourceURL(for: $0) != nil }) else {
            throw NSError(domain: "SattariHub", code: 2, userInfo: [NSLocalizedDescriptionKey: "No local, bundled, or downloaded package payload found for \(product.name)."])
        }

        var installedCount = 0
        var missing: [String] = []
        for bundle in product.bundles {
            guard let source = sourceURL(for: bundle), fileManager.fileExists(atPath: source.path) else {
                missing.append("\(bundle.kind): \(bundle.relativeSourcePath)")
                continue
            }

            let destination = destinationURL(for: bundle)
            try fileManager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)

            if fileManager.fileExists(atPath: destination.path) {
                let backupRoot = appSupport.appendingPathComponent("Backups/\(product.id)", isDirectory: true)
                try fileManager.createDirectory(at: backupRoot, withIntermediateDirectories: true)
                let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
                let backup = backupRoot.appendingPathComponent("\(stamp)-\(destination.lastPathComponent)")
                try? fileManager.removeItem(at: backup)
                try fileManager.moveItem(at: destination, to: backup)
            }

            try fileManager.copyItem(at: source, to: destination)
            normalizeInstalledBundle(at: destination)
            installedCount += 1
        }

        if installedCount == 0 {
            throw NSError(domain: "SattariHub", code: 3, userInfo: [NSLocalizedDescriptionKey: "No installable bundles were found for \(product.name). Missing: \(missing.joined(separator: "; "))"])
        }

        receipts[product.id] = InstallReceipt(
            productId: product.id,
            version: product.version,
            installedAt: ISO8601DateFormatter().string(from: Date()),
            bundleCount: installedCount,
            updateChannel: manifest.defaultUpdateChannel
        )
        try saveReceipts()

        let missingNote = missing.isEmpty ? "" : " Missing/skipped: \(missing.count)."
        return "Installed \(installedCount) bundle(s) for \(product.name). Gatekeeper repair applied.\(missingNote) Restart or rescan your DAW."
    }

    private func runRepairTool(_ executable: String, _ arguments: [String]) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        try? process.run()
        process.waitUntilExit()
    }

    private func normalizeInstalledBundle(at url: URL) {
        // Downloaded/internal-alpha bundles can inherit quarantine from the Hub archive.
        // Ableton/Live may then show “can’t be opened” even when the plugin validates.
        runRepairTool("/usr/bin/xattr", ["-cr", url.path])
        runRepairTool("/bin/chmod", ["-R", "u+rwX,go+rX", url.path])
    }

    private func archiveCacheName(for url: URL) -> String {
        var name = url.lastPathComponent
        if name.hasSuffix(".tar.gz") { name.removeLast(7) }
        return name.replacingOccurrences(of: "/", with: "-")
    }

    func prepareRemotePayloadIfAvailable(for product: HubProduct) throws {
        guard let archiveURLString = product.updateArchiveURL,
              let archiveURL = URL(string: archiveURLString) else {
            throw NSError(domain: "SattariHub", code: 4, userInfo: [NSLocalizedDescriptionKey: "No online update archive configured for \(product.name)."])
        }

        let downloadsDir = appSupport.appendingPathComponent("Downloads", isDirectory: true)
        let payloadsDir = appSupport.appendingPathComponent("RemotePayloads", isDirectory: true)
        try fileManager.createDirectory(at: downloadsDir, withIntermediateDirectories: true)
        try fileManager.createDirectory(at: payloadsDir, withIntermediateDirectories: true)

        let archivePath = downloadsDir.appendingPathComponent(archiveURL.lastPathComponent)
        let payloadRoot = payloadsDir.appendingPathComponent(archiveCacheName(for: archiveURL), isDirectory: true)

        if !fileManager.fileExists(atPath: payloadRoot.path) {
            let data = try Data(contentsOf: archiveURL)
            try data.write(to: archivePath, options: [.atomic])
            try? fileManager.removeItem(at: payloadRoot)
            try fileManager.createDirectory(at: payloadRoot, withIntermediateDirectories: true)

            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
            process.arguments = ["-xzf", archivePath.path, "-C", payloadRoot.path]
            try process.run()
            process.waitUntilExit()
            if process.terminationStatus != 0 {
                throw NSError(domain: "SattariHub", code: 5, userInfo: [NSLocalizedDescriptionKey: "Could not extract update archive for \(product.name)."])
            }
        }
    }

    func saveReceipts() throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(receipts)
        try data.write(to: receiptsURL, options: [.atomic])
    }
}

final class HubWindowController: NSWindowController {
    private let model: HubModel
    private let stack = NSStackView()
    private let statusLabel = NSTextField(labelWithString: "")

    init(model: HubModel) {
        self.model = model
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 980, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Sattari Hub"
        window.center()
        super.init(window: window)
        buildUI()
    }

    required init?(coder: NSCoder) { nil }

    private func buildUI() {
        guard let contentView = window?.contentView else { return }
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = NSColor(calibratedRed: 0.035, green: 0.04, blue: 0.065, alpha: 1).cgColor

        let root = NSStackView()
        root.orientation = .vertical
        root.spacing = 16
        root.edgeInsets = NSEdgeInsets(top: 24, left: 24, bottom: 24, right: 24)
        root.translatesAutoresizingMaskIntoConstraints = false
        contentView.addSubview(root)
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            root.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            root.topAnchor.constraint(equalTo: contentView.topAnchor),
            root.bottomAnchor.constraint(equalTo: contentView.bottomAnchor)
        ])

        let title = NSTextField(labelWithString: model.manifest.suiteName)
        title.font = NSFont.boldSystemFont(ofSize: 30)
        title.textColor = .white
        root.addArrangedSubview(title)

        let subtitle = NSTextField(labelWithString: "Installer + updater hub • \(model.manifest.suiteVersion) • \(model.manifest.defaultUpdateChannel)")
        subtitle.font = NSFont.systemFont(ofSize: 14, weight: .medium)
        subtitle.textColor = NSColor(calibratedWhite: 0.75, alpha: 1)
        root.addArrangedSubview(subtitle)

        let truth = NSTextField(labelWithString: model.manifest.truthLabel)
        truth.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        truth.textColor = NSColor(calibratedRed: 0.2, green: 0.95, blue: 0.82, alpha: 1)
        root.addArrangedSubview(truth)

        statusLabel.font = NSFont.systemFont(ofSize: 13, weight: .medium)
        statusLabel.textColor = NSColor(calibratedWhite: 0.85, alpha: 1)
        let bundledCount = model.manifest.products.filter { model.productIsAvailable($0) }.count
        statusLabel.stringValue = model.repoRoot == nil
            ? "Bundled package mode: \(bundledCount) product(s) available inside this Hub."
            : "Package root: \(model.repoRoot!.path) • bundled/local available products: \(bundledCount)"
        root.addArrangedSubview(statusLabel)

        let scrollView = NSScrollView()
        scrollView.hasVerticalScroller = true
        scrollView.borderType = .noBorder
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        root.addArrangedSubview(scrollView)
        scrollView.heightAnchor.constraint(greaterThanOrEqualToConstant: 480).isActive = true

        stack.orientation = .vertical
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        scrollView.documentView = stack
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: scrollView.contentView.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: scrollView.contentView.trailingAnchor),
            stack.topAnchor.constraint(equalTo: scrollView.contentView.topAnchor),
            stack.widthAnchor.constraint(equalTo: scrollView.contentView.widthAnchor)
        ])
        reloadProductCards()
    }

    private func reloadProductCards() {
        stack.arrangedSubviews.forEach { view in
            stack.removeArrangedSubview(view)
            view.removeFromSuperview()
        }

        for product in model.manifest.products {
            stack.addArrangedSubview(card(for: product))
        }
    }

    private static func accentColor(_ hex: String?, fallback: NSColor = NSColor(calibratedRed: 0.2, green: 0.95, blue: 0.82, alpha: 1)) -> NSColor {
        guard var s = hex else { return fallback }
        if s.hasPrefix("#") { s.removeFirst() }
        guard s.count == 6, let v = UInt32(s, radix: 16) else { return fallback }
        return NSColor(calibratedRed: CGFloat((v >> 16) & 0xff) / 255.0,
                       green: CGFloat((v >> 8) & 0xff) / 255.0,
                       blue: CGFloat(v & 0xff) / 255.0,
                       alpha: 1)
    }

    private func card(for product: HubProduct) -> NSView {
        let accent = Self.accentColor(product.accent)

        let box = NSBox()
        box.boxType = .custom
        box.cornerRadius = 14
        box.borderWidth = 1
        box.borderColor = accent.withAlphaComponent(0.32)
        box.fillColor = NSColor(calibratedRed: 0.07, green: 0.09, blue: 0.13, alpha: 1)

        let row = NSStackView()
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 18
        row.edgeInsets = NSEdgeInsets(top: 14, left: 16, bottom: 14, right: 16)
        row.translatesAutoresizingMaskIntoConstraints = false
        box.contentView?.addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: box.contentView!.leadingAnchor),
            row.trailingAnchor.constraint(equalTo: box.contentView!.trailingAnchor),
            row.topAnchor.constraint(equalTo: box.contentView!.topAnchor),
            row.bottomAnchor.constraint(equalTo: box.contentView!.bottomAnchor)
        ])

        // Per-product accent stripe (shared brand colour).
        let accentBar = NSView()
        accentBar.wantsLayer = true
        accentBar.layer?.backgroundColor = accent.cgColor
        accentBar.layer?.cornerRadius = 2.5
        accentBar.translatesAutoresizingMaskIntoConstraints = false
        accentBar.widthAnchor.constraint(equalToConstant: 5).isActive = true
        accentBar.heightAnchor.constraint(equalToConstant: 48).isActive = true
        row.addArrangedSubview(accentBar)

        let copy = NSStackView()
        copy.orientation = .vertical
        copy.spacing = 5
        row.addArrangedSubview(copy)

        let name = NSTextField(labelWithString: product.name)
        name.font = NSFont.boldSystemFont(ofSize: 18)
        name.textColor = .white
        copy.addArrangedSubview(name)

        let meta = NSTextField(labelWithString: "\(product.category) • \(product.formats.joined(separator: ", ")) • \(product.version)")
        meta.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        meta.textColor = accent
        copy.addArrangedSubview(meta)

        let desc = NSTextField(labelWithString: product.description)
        desc.font = NSFont.systemFont(ofSize: 12)
        desc.textColor = NSColor(calibratedWhite: 0.74, alpha: 1)
        copy.addArrangedSubview(desc)

        let state = NSTextField(labelWithString: model.statusText(for: product))
        state.font = NSFont.systemFont(ofSize: 11)
        state.textColor = NSColor(calibratedWhite: 0.62, alpha: 1)
        copy.addArrangedSubview(state)

        let button = NSButton(title: model.productNeedsUpdate(product) ? "Install / Update" : "Reinstall", target: self, action: #selector(installClicked(_:)))
        button.identifier = NSUserInterfaceItemIdentifier(product.id)
        button.bezelStyle = .rounded
        button.isEnabled = model.productIsAvailable(product) || model.productCanDownloadUpdate(product)
        row.addArrangedSubview(button)

        return box
    }

    @objc private func installClicked(_ sender: NSButton) {
        guard let productId = sender.identifier?.rawValue,
              let product = model.manifest.products.first(where: { $0.id == productId }) else { return }
        do {
            statusLabel.stringValue = try model.install(product: product)
            reloadProductCards()
        } catch {
            statusLabel.stringValue = "Install failed: \(error.localizedDescription)"
            NSSound.beep()
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var controller: HubWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        do {
            controller = HubWindowController(model: try HubModel())
            controller?.showWindow(nil)
            NSApp.activate(ignoringOtherApps: true)
        } catch {
            let alert = NSAlert(error: error)
            alert.runModal()
            NSApp.terminate(nil)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}

if CommandLine.arguments.contains("--contract-check") {
    do {
        let model = try HubModel()
        let productCount = model.manifest.products.count
        let bundleCount = model.manifest.products.reduce(0) { $0 + $1.bundles.count }
        guard productCount >= 6 else { throw NSError(domain: "SattariHub", code: 10, userInfo: [NSLocalizedDescriptionKey: "Expected at least 6 products, got \(productCount)"]) }
        guard model.manifest.products.contains(where: { $0.id == "auto-pitch" }) else { throw NSError(domain: "SattariHub", code: 11, userInfo: [NSLocalizedDescriptionKey: "Missing Auto Pitch product"]) }
        guard model.manifest.products.flatMap({ $0.bundles }).contains(where: { $0.kind == "VST3" }) else { throw NSError(domain: "SattariHub", code: 12, userInfo: [NSLocalizedDescriptionKey: "Missing VST3 bundle entry"]) }
        let available = model.manifest.products.filter { model.productIsAvailable($0) }.count
        let downloadable = model.manifest.products.filter { model.productCanDownloadUpdate($0) }.count
        if CommandLine.arguments.contains("--require-online-updates") {
            guard downloadable >= productCount else { throw NSError(domain: "SattariHub", code: 13, userInfo: [NSLocalizedDescriptionKey: "Expected online update archives for all products, got \(downloadable)/\(productCount)"]) }
        }
        if CommandLine.arguments.contains("--download-contract-check") {
            guard let product = model.manifest.products.first(where: { $0.id == "auto-pitch" }) else {
                throw NSError(domain: "SattariHub", code: 14, userInfo: [NSLocalizedDescriptionKey: "Missing Auto Pitch for download contract"])
            }
            try model.prepareRemotePayloadIfAvailable(for: product)
            guard product.bundles.contains(where: { model.sourceURL(for: $0) != nil }) else {
                throw NSError(domain: "SattariHub", code: 15, userInfo: [NSLocalizedDescriptionKey: "Downloaded archive did not expose Auto Pitch payload"])
            }
        }
        print("SattariHub contract ok: products=\(productCount) bundles=\(bundleCount) availableProducts=\(available) downloadableProducts=\(downloadable) channel=\(model.manifest.defaultUpdateChannel)")
        exit(0)
    } catch {
        fputs("SattariHub contract failed: \(error.localizedDescription)\n", stderr)
        exit(1)
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()

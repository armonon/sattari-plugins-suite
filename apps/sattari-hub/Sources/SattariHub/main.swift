import AppKit
import CryptoKit
import Foundation

struct HubManifest: Codable {
    let schemaVersion: Int
    let suiteName: String
    let suiteVersion: String
    let truthLabel: String
    let defaultUpdateChannel: String
    let updateFeedURL: String?
    let updateFeedSHA256: String?
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
    let updateArchiveSHA256: String?
    let updateArchiveBytes: Int64?
    let lifecycle: String?
    let shippingProfile: String?
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
           let expectedHash = bundledManifest.updateFeedSHA256,
           Self.isValidSHA256(expectedHash),
           let data = try? Data(contentsOf: feedURL),
           Self.sha256(data) == expectedHash.lowercased(),
           let remoteManifest = try? decoder.decode(HubManifest.self, from: data),
           remoteManifest.schemaVersion == bundledManifest.schemaVersion,
           remoteManifest.products.count >= bundledManifest.products.count {
            return remoteManifest
        }

        return bundledManifest
    }

    static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func isValidSHA256(_ value: String) -> Bool {
        value.count == 64 && value.allSatisfy { $0.isHexDigit }
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
        var repositoryCandidates: [URL?] = []
        let autoPitchPrefix = "build/all/plugins/auto-pitch/"
        if bundle.relativeSourcePath.hasPrefix(autoPitchPrefix) {
            let suffix = String(bundle.relativeSourcePath.dropFirst(autoPitchPrefix.count))
            if let explicit = ProcessInfo.processInfo.environment["SATTARI_AUTOPITCH_BUILD_DIR"],
               !explicit.isEmpty {
                repositoryCandidates.append(
                    URL(fileURLWithPath: explicit, isDirectory: true).appendingPathComponent(suffix)
                )
            }
            repositoryCandidates.append(
                repoRoot?.appendingPathComponent("build/auto-pitch-neural").appendingPathComponent(suffix)
            )
        }
        repositoryCandidates.append(repoRoot?.appendingPathComponent(bundle.relativeSourcePath))

        var candidates: [URL?] = repositoryCandidates + [
            Bundle.main.resourceURL?.appendingPathComponent("Payload").appendingPathComponent(bundle.relativeSourcePath),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                .appendingPathComponent("Payload")
                .appendingPathComponent(bundle.relativeSourcePath)
        ]

        for root in remotePayloadCandidateRoots() {
            candidates.append(root.appendingPathComponent("Payload").appendingPathComponent(bundle.relativeSourcePath))
            candidates.append(root.appendingPathComponent(bundle.relativeSourcePath))
        }

        return candidates.compactMap { $0 }.first { bundleIsUsable(at: $0, bundle: bundle) }
    }

    private func bundleIsUsable(at url: URL, bundle: InstallBundle) -> Bool {
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: url.path, isDirectory: &isDirectory),
              isDirectory.boolValue,
              (try? url.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) != true else {
            return false
        }

        let expectedExtension: String
        switch bundle.kind {
        case "AU": expectedExtension = "component"
        case "VST3": expectedExtension = "vst3"
        case "Standalone": expectedExtension = "app"
        default: return false
        }
        guard url.pathExtension.lowercased() == expectedExtension,
              fileManager.fileExists(atPath: url.appendingPathComponent("Contents/Info.plist").path) else {
            return false
        }

        let executableDir = url.appendingPathComponent("Contents/MacOS", isDirectory: true)
        guard let executables = try? fileManager.contentsOfDirectory(
            at: executableDir, includingPropertiesForKeys: [.isRegularFileKey]),
              executables.contains(where: {
                  (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
                      && fileManager.isExecutableFile(atPath: $0.path)
              }) else {
            return false
        }
        return true
    }

    func destinationURL(for bundle: InstallBundle) -> URL {
        fileManager.homeDirectoryForCurrentUser.appendingPathComponent(bundle.installSubpath)
    }

    func productIsAvailable(_ product: HubProduct) -> Bool {
        !product.bundles.isEmpty && product.bundles.allSatisfy { sourceURL(for: $0) != nil }
    }

    func productCanDownloadUpdate(_ product: HubProduct) -> Bool {
        product.updateArchiveURL != nil
            && product.updateArchiveSHA256.map(Self.isValidSHA256) == true
            && product.updateArchiveBytes.map { $0 > 0 } == true
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

    private struct PreparedBundle {
        let bundle: InstallBundle
        let staged: URL
        let destination: URL
    }

    private struct CommittedBundle {
        let destination: URL
        let backup: URL?
    }

    func install(product: HubProduct) throws -> String {
        if !productIsAvailable(product) {
            try prepareRemotePayloadIfAvailable(for: product)
        }

        let sources = product.bundles.map { sourceURL(for: $0) }
        guard !product.bundles.isEmpty, sources.allSatisfy({ $0 != nil }) else {
            let missing = zip(product.bundles, sources)
                .filter { $0.1 == nil }
                .map { "\($0.0.kind): \($0.0.relativeSourcePath)" }
                .joined(separator: "; ")
            throw NSError(domain: "SattariHub", code: 2, userInfo: [
                NSLocalizedDescriptionKey:
                    "The package for \(product.name) is incomplete. Nothing was installed. Missing or invalid: \(missing)"
            ])
        }

        let stagingRoot = appSupport
            .appendingPathComponent("InstallStaging", isDirectory: true)
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try fileManager.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
        defer { try? fileManager.removeItem(at: stagingRoot) }

        var prepared: [PreparedBundle] = []
        for (index, bundle) in product.bundles.enumerated() {
            let source = sources[index]!
            let staged = stagingRoot.appendingPathComponent(
                "\(index)-\(source.lastPathComponent)", isDirectory: true)
            try fileManager.copyItem(at: source, to: staged)
            guard bundleIsUsable(at: staged, bundle: bundle) else {
                throw NSError(domain: "SattariHub", code: 3, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Staged \(bundle.kind) payload failed bundle validation. Nothing was installed."
                ])
            }
            prepared.append(PreparedBundle(
                bundle: bundle,
                staged: staged,
                destination: destinationURL(for: bundle)))
        }

        let backupRoot = appSupport.appendingPathComponent(
            "Backups/\(product.id)", isDirectory: true)
        try fileManager.createDirectory(at: backupRoot, withIntermediateDirectories: true)
        let stamp = ISO8601DateFormatter().string(from: Date())
            .replacingOccurrences(of: ":", with: "-")
        var committed: [CommittedBundle] = []

        do {
            for (index, item) in prepared.enumerated() {
                try fileManager.createDirectory(
                    at: item.destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true)

                var backup: URL?
                if fileManager.fileExists(atPath: item.destination.path) {
                    let candidate = backupRoot.appendingPathComponent(
                        "\(stamp)-\(index)-\(item.destination.lastPathComponent)")
                    try? fileManager.removeItem(at: candidate)
                    try fileManager.moveItem(at: item.destination, to: candidate)
                    backup = candidate
                }

                do {
                    try fileManager.moveItem(at: item.staged, to: item.destination)
                    ensureInstalledPermissions(at: item.destination)
                    guard bundleIsUsable(at: item.destination, bundle: item.bundle) else {
                        throw NSError(domain: "SattariHub", code: 4, userInfo: [
                            NSLocalizedDescriptionKey:
                                "Installed \(item.bundle.kind) bundle failed post-copy validation."
                        ])
                    }
                    committed.append(CommittedBundle(
                        destination: item.destination, backup: backup))
                } catch {
                    try? fileManager.removeItem(at: item.destination)
                    if let backup { try? fileManager.moveItem(at: backup, to: item.destination) }
                    throw error
                }
            }
        } catch {
            for item in committed.reversed() {
                try? fileManager.removeItem(at: item.destination)
                if let backup = item.backup {
                    try? fileManager.moveItem(at: backup, to: item.destination)
                }
            }
            throw error
        }

        let superseded = retireSupersededBundles(
            installed: committed.map(\.destination), backupRoot: backupRoot, stamp: stamp)

        receipts[product.id] = InstallReceipt(
            productId: product.id,
            version: product.version,
            installedAt: ISO8601DateFormatter().string(from: Date()),
            bundleCount: prepared.count,
            updateChannel: manifest.defaultUpdateChannel
        )
        try saveReceipts()

        var message = "Installed and verified \(prepared.count) bundle(s) for \(product.name)."
        if superseded > 0 {
            message += " Retired \(superseded) superseded bundle(s) claiming the same plug-in ID."
        }
        return message + " Restart or rescan your DAW."
    }

    /// A plug-in's real identity is its AU type/subtype/manufacturer or its VST3
    /// class ID — never its file name. Both are derived from the four-character
    /// codes, so RENAMING A PRODUCT DOES NOT CHANGE THEM.
    ///
    /// That is what makes a rename dangerous: "Sattari Brain" became "Sattari
    /// Compass", the Hub installed the new bundle beside the old one, and both
    /// claimed aumf/Br01/Satr and VST3 CID ...SatrBr01. macOS keys the component
    /// registry on that identity, so which one a DAW loaded was undefined — and
    /// the stale one was the older, buggier binary.
    private func bundleIdentity(at url: URL) -> String? {
        switch url.pathExtension.lowercased() {
        case "component":
            guard let data = try? Data(contentsOf: url.appendingPathComponent("Contents/Info.plist")),
                  let plist = try? PropertyListSerialization.propertyList(
                      from: data, options: [], format: nil) as? [String: Any],
                  let components = plist["AudioComponents"] as? [[String: Any]],
                  let first = components.first,
                  let type = first["type"] as? String,
                  let subtype = first["subtype"] as? String,
                  let manufacturer = first["manufacturer"] as? String
            else { return nil }
            return "au:\(type)/\(subtype)/\(manufacturer)"

        case "vst3":
            // moduleinfo.json is JSON5-ish (JUCE emits trailing commas), so
            // JSONSerialization rejects it. Scan for the CID rather than parse.
            let info = url.appendingPathComponent("Contents/Resources/moduleinfo.json")
            guard let text = try? String(contentsOf: info, encoding: .utf8),
                  let range = text.range(of: #""CID"\s*:\s*"[0-9A-Fa-f]+""#, options: .regularExpression),
                  let value = text[range].split(separator: "\"").last
            else { return nil }
            return "vst3:\(value.uppercased())"

        default:
            return nil   // a Standalone .app has no shared registry to collide in
        }
    }

    /// Move aside any OTHER bundle that claims the same identity as one we just
    /// installed. Identity-based rather than a hardcoded rename list, so it keeps
    /// working for renames nobody has made yet. Retired, not deleted: they go to
    /// the same backup root the upgrade path already uses.
    private func retireSupersededBundles(
        installed: [URL], backupRoot: URL, stamp: String) -> Int
    {
        var retired = 0
        for destination in installed {
            guard let identity = bundleIdentity(at: destination) else { continue }
            let folder = destination.deletingLastPathComponent()
            guard let siblings = try? fileManager.contentsOfDirectory(
                at: folder, includingPropertiesForKeys: nil) else { continue }

            for sibling in siblings
            where sibling.lastPathComponent != destination.lastPathComponent
                && sibling.pathExtension == destination.pathExtension
            {
                guard bundleIdentity(at: sibling) == identity else { continue }
                let candidate = backupRoot.appendingPathComponent(
                    "\(stamp)-superseded-\(sibling.lastPathComponent)")
                try? fileManager.removeItem(at: candidate)
                if (try? fileManager.moveItem(at: sibling, to: candidate)) != nil {
                    retired += 1
                }
            }
        }
        return retired
    }

    private func runRepairTool(_ executable: String, _ arguments: [String]) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        try? process.run()
        process.waitUntilExit()
    }

    private func ensureInstalledPermissions(at url: URL) {
        // Do not strip quarantine. Signed/notarized downloads should pass normal
        // Gatekeeper verification; silently removing provenance is not an update
        // mechanism. We only ensure user-readable/executable bundle permissions.
        runRepairTool("/bin/chmod", ["-R", "u+rwX,go+rX", url.path])
    }

    private func archiveCacheName(for url: URL) -> String {
        var name = url.lastPathComponent
        if name.hasSuffix(".tar.gz") { name.removeLast(7) }
        return name.replacingOccurrences(of: "/", with: "-")
    }

    private func runToolCapturingOutput(
        _ executable: String, _ arguments: [String]
    ) throws -> (Int32, String) {
        let process = Process()
        let pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = pipe
        try process.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        return (process.terminationStatus,
                String(data: data, encoding: .utf8) ?? "")
    }

    func prepareRemotePayloadIfAvailable(for product: HubProduct) throws {
        guard let archiveURLString = product.updateArchiveURL,
              let archiveURL = URL(string: archiveURLString),
              let expectedHash = product.updateArchiveSHA256,
              Self.isValidSHA256(expectedHash),
              let expectedBytes = product.updateArchiveBytes,
              expectedBytes > 0 else {
            throw NSError(domain: "SattariHub", code: 5, userInfo: [
                NSLocalizedDescriptionKey:
                    "No integrity-pinned online archive is configured for \(product.name)."
            ])
        }

        let downloadsDir = appSupport.appendingPathComponent("Downloads", isDirectory: true)
        let payloadsDir = appSupport.appendingPathComponent("RemotePayloads", isDirectory: true)
        try fileManager.createDirectory(at: downloadsDir, withIntermediateDirectories: true)
        try fileManager.createDirectory(at: payloadsDir, withIntermediateDirectories: true)

        let archivePath = downloadsDir.appendingPathComponent(archiveURL.lastPathComponent)
        let payloadRoot = payloadsDir.appendingPathComponent(
            "\(archiveCacheName(for: archiveURL))-\(expectedHash.prefix(12))",
            isDirectory: true)

        if !fileManager.fileExists(atPath: payloadRoot.path) {
            let data = try Data(contentsOf: archiveURL)
            guard data.count == expectedBytes else {
                throw NSError(domain: "SattariHub", code: 6, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Downloaded archive size mismatch for \(product.name)."
                ])
            }
            guard Self.sha256(data) == expectedHash.lowercased() else {
                throw NSError(domain: "SattariHub", code: 7, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Downloaded archive checksum mismatch for \(product.name)."
                ])
            }
            try data.write(to: archivePath, options: [.atomic])
            try? fileManager.removeItem(at: payloadRoot)
            try fileManager.createDirectory(at: payloadRoot, withIntermediateDirectories: true)

            let (listStatus, listOutput) = try runToolCapturingOutput(
                "/usr/bin/tar", ["-tzf", archivePath.path])
            guard listStatus == 0 else {
                throw NSError(domain: "SattariHub", code: 8, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Could not inspect update archive for \(product.name)."
                ])
            }
            for entry in listOutput.split(separator: "\n").map(String.init) {
                let components = entry.split(separator: "/", omittingEmptySubsequences: false)
                guard !entry.hasPrefix("/"), !components.contains("..") else {
                    throw NSError(domain: "SattariHub", code: 9, userInfo: [
                        NSLocalizedDescriptionKey:
                            "Unsafe path in update archive for \(product.name)."
                    ])
                }
            }

            let (verboseStatus, verboseOutput) = try runToolCapturingOutput(
                "/usr/bin/tar", ["-tvzf", archivePath.path])
            guard verboseStatus == 0,
                  !verboseOutput.split(separator: "\n").contains(where: {
                      guard let first = $0.first else { return false }
                      return first == "l" || first == "h"
                  }) else {
                throw NSError(domain: "SattariHub", code: 10, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Update archive contains unsupported links for \(product.name)."
                ])
            }

            let (extractStatus, _) = try runToolCapturingOutput(
                "/usr/bin/tar", ["-xzf", archivePath.path, "-C", payloadRoot.path])
            if extractStatus != 0 {
                try? fileManager.removeItem(at: payloadRoot)
                throw NSError(domain: "SattariHub", code: 11, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Could not extract update archive for \(product.name)."
                ])
            }
        }

        guard productIsAvailable(product) else {
            try? fileManager.removeItem(at: payloadRoot)
            throw NSError(domain: "SattariHub", code: 12, userInfo: [
                NSLocalizedDescriptionKey:
                    "Verified archive for \(product.name) did not contain every declared bundle."
            ])
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

        if let feed = model.manifest.updateFeedURL, !feed.isEmpty {
            let host = URL(string: feed)?.host ?? feed
            let feedLine = NSTextField(labelWithString: "Update feed: \(host)")
            feedLine.font = NSFont.systemFont(ofSize: 11)
            feedLine.textColor = NSColor(calibratedWhite: 0.55, alpha: 1)
            root.addArrangedSubview(feedLine)
        }

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

        // Batch actions are limited to the explicitly curated alpha profile. A
        // 49-product "Install All" makes unvalidated experiments look shippable.
        let actionRow = NSStackView()
        actionRow.orientation = .horizontal
        actionRow.spacing = 12
        let installAll = NSButton(title: "Install Alpha Set", target: self, action: #selector(installAllClicked(_:)))
        installAll.bezelStyle = .rounded
        installAll.keyEquivalent = "\r"
        let updateAll = NSButton(title: "Update All", target: self, action: #selector(updateAllClicked(_:)))
        updateAll.bezelStyle = .rounded
        actionRow.addArrangedSubview(installAll)
        actionRow.addArrangedSubview(updateAll)
        root.addArrangedSubview(actionRow)

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

    @objc private func installAllClicked(_ sender: NSButton) {
        let alphaSet = model.manifest.products.filter { $0.shippingProfile == "alpha" }
        guard !alphaSet.isEmpty else {
            statusLabel.stringValue = "No products are assigned to the curated alpha set."
            return
        }
        installMany(products: alphaSet, label: "Install Alpha Set")
    }

    @objc private func updateAllClicked(_ sender: NSButton) {
        let needsUpdate = model.manifest.products.filter {
            model.receipts[$0.id] != nil && model.productNeedsUpdate($0)
        }
        if needsUpdate.isEmpty {
            statusLabel.stringValue = "All products are up to date."
            return
        }
        installMany(products: needsUpdate, label: "Update All")
    }

    private func installMany(products: [HubProduct], label: String) {
        var installed = 0
        var failed: [String] = []
        for product in products {
            do {
                _ = try model.install(product: product)
                installed += 1
            } catch {
                failed.append("\(product.name) (\(error.localizedDescription))")
            }
        }
        if failed.isEmpty {
            statusLabel.stringValue = "\(label): installed \(installed) of \(products.count) product(s). Restart or rescan your DAW."
        } else {
            statusLabel.stringValue = "\(label): \(installed)/\(products.count) installed. Failed: \(failed.joined(separator: "; "))"
            NSSound.beep()
        }
        reloadProductCards()
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
        guard model.manifest.products.contains(where: { $0.id == "vocal" }) else { throw NSError(domain: "SattariHub", code: 11, userInfo: [NSLocalizedDescriptionKey: "Missing Vocal product (Auto Pitch lives here as a module)"]) }
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

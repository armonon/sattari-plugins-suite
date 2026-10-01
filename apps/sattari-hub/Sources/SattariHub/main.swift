import AppKit
import CryptoKit
import Foundation

struct HubManifest: Codable, Sendable {
    let schemaVersion: Int
    let suiteName: String
    let suiteVersion: String
    let truthLabel: String
    let defaultUpdateChannel: String
    let updateFeedURL: String?
    let updateFeedSHA256: String?
    let suite: [HubSuite]
    let products: [HubProduct]
}

struct HubSuite: Codable, Sendable {
    let id: String
    let name: String
    let verb: String
    let accent: String
    let tier: String
    let tagline: String
    let primary: String
}

struct HubProduct: Codable, Sendable {
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
    let suite: String?
    let suiteRole: String?
}

struct InstallBundle: Codable, Sendable {
    let kind: String
    let relativeSourcePath: String
    let installSubpath: String

    var hasSafePaths: Bool {
        func relative(_ path: String) -> Bool {
            !path.isEmpty && !path.hasPrefix("/") && !path.contains("\\")
                && !path.contains("\0") && path.split(separator: "/", omittingEmptySubsequences: false)
                    .allSatisfy { !$0.isEmpty && $0 != "." && $0 != ".." }
        }
        let directory: String
        let suffix: String
        switch kind {
        case "AU": directory = "Library/Audio/Plug-Ins/Components/"; suffix = ".component"
        case "VST3": directory = "Library/Audio/Plug-Ins/VST3/"; suffix = ".vst3"
        case "Standalone": directory = "Applications/"; suffix = ".app"
        default: return false
        }
        guard relative(relativeSourcePath), relative(installSubpath),
              relativeSourcePath.hasSuffix(suffix), installSubpath.hasPrefix(directory) else { return false }
        let filename = String(installSubpath.dropFirst(directory.count))
        return !filename.contains("/") && filename.count > suffix.count && filename.hasSuffix(suffix)
    }
}

struct InstallReceipt: Codable {
    let productId: String
    let version: String
    let installedAt: String
    let bundleCount: Int
    let updateChannel: String
}

enum SessionIdentity {
    static let defaultsKey = "SattariSessionIdentity"

    static func sanitise(_ value: String) -> String {
        // Keep this byte-for-byte compatible with the C++ session bus.
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
        let filtered = String(value.unicodeScalars.filter { allowed.contains($0) })
        return String(filtered.prefix(48))
    }

    static func current() -> String {
        if let saved = UserDefaults.standard.string(forKey: defaultsKey) {
            let clean = sanitise(saved)
            if !clean.isEmpty { return clean }
        }
        let generated = "project-" + String(UUID().uuidString.lowercased().prefix(8))
        UserDefaults.standard.set(generated, forKey: defaultsKey)
        return generated
    }

    static func save(_ value: String) -> String {
        let clean = sanitise(value)
        let result = clean.isEmpty ? current() : clean
        UserDefaults.standard.set(result, forKey: defaultsKey)
        return result
    }

    static func installedDAWs() -> [URL] {
        let applications = URL(fileURLWithPath: "/Applications", isDirectory: true)
        let names = [
            "Logic Pro.app", "Logic Pro Creator Studio.app", "Ableton Live 12 Suite.app",
            "Ableton Live 12 Standard.app", "REAPER.app", "Studio One 7.app",
            "Pro Tools.app", "FL Studio 2026.app", "FL Studio.app"
        ]
        return names.map { applications.appendingPathComponent($0, isDirectory: true) }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
    }

    static func launch(_ application: URL, sessionId: String) throws {
        let clean = save(sessionId)
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        process.arguments = ["-n", "--env", "SATTARI_SESSION_ID=\(clean)", application.path]
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw NSError(domain: "SattariHub", code: 30, userInfo: [
                NSLocalizedDescriptionKey: "macOS could not launch \(application.deletingPathExtension().lastPathComponent)."
            ])
        }
    }
}

struct HubInstallConfiguration: Sendable {
    let manifest: HubManifest
    let storageRoot: URL
    let installationRoot: URL
    let sourceRoot: URL?
}

final class HubModel {
    let manifest: HubManifest
    let repoRoot: URL?
    private let fileManager = FileManager.default
    private let appSupport: URL
    private let receiptsURL: URL
    private let installationRoot: URL
    private(set) var receipts: [String: InstallReceipt]

    var installConfiguration: HubInstallConfiguration {
        HubInstallConfiguration(manifest: manifest, storageRoot: appSupport,
                                installationRoot: installationRoot, sourceRoot: repoRoot)
    }

    func reloadReceipts() { receipts = Self.loadReceipts(from: receiptsURL) }

    init(manifest suppliedManifest: HubManifest? = nil, storageRoot: URL? = nil,
         installationRoot suppliedInstallationRoot: URL? = nil, sourceRoot: URL? = nil) throws {
        manifest = try suppliedManifest ?? Self.loadManifest()
        try Self.validateManifest(manifest)
        repoRoot = sourceRoot ?? Self.resolveRepoRoot()
        installationRoot = suppliedInstallationRoot ?? fileManager.homeDirectoryForCurrentUser
        appSupport = storageRoot ?? fileManager.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Sattari Hub", isDirectory: true)
        receiptsURL = appSupport.appendingPathComponent("receipts.json")
        try fileManager.createDirectory(at: appSupport, withIntermediateDirectories: true)
        receipts = Self.loadReceipts(from: receiptsURL)
    }

    static func loadManifest() throws -> HubManifest {
        let decoder = JSONDecoder()
        let candidates: [URL?] = [
            Bundle.main.resourceURL?.appendingPathComponent("sattari-products.json"),
            // SwiftPM places resources in a sibling bundle, while the packaged
            // .app uses Contents/Resources directly. Do not eagerly evaluate
            // Bundle.module: it traps if the optional SwiftPM bundle is absent.
            Bundle(url: Bundle.main.bundleURL.appendingPathComponent("SattariHub_SattariHub.bundle"))?
                .resourceURL?.appendingPathComponent("sattari-products.json"),
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
        try validateManifest(bundledManifest)

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
           remoteManifest.products.count >= bundledManifest.products.count,
           (try? validateManifest(remoteManifest)) != nil {
            return remoteManifest
        }

        return bundledManifest
    }

    static func validateManifest(_ manifest: HubManifest) throws {
        var ids = Set<String>(), destinations = Set<String>()
        guard manifest.schemaVersion == 1 else {
            throw NSError(domain: "SattariHub", code: 17, userInfo: [NSLocalizedDescriptionKey: "Unsupported package manifest version."])
        }
        for product in manifest.products {
            guard !product.id.isEmpty, SessionIdentity.sanitise(product.id) == product.id,
                  ids.insert(product.id).inserted,
                  product.bundles.allSatisfy({ $0.hasSafePaths && destinations.insert($0.installSubpath.lowercased()).inserted }) else {
                throw NSError(domain: "SattariHub", code: 18, userInfo: [NSLocalizedDescriptionKey: "Unsafe or duplicate package paths in the manifest. Nothing was installed."])
            }
        }
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
        guard bundle.hasSafePaths else { return nil }
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
        installationRoot.appendingPathComponent(bundle.installSubpath)
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

    func install(product: HubProduct, control: InstallControl = InstallControl()) throws -> String {
        try control.checkpoint("Checking \(product.name)…")
        guard manifest.products.contains(where: { $0.id == product.id }),
              product.bundles.allSatisfy(\.hasSafePaths),
              SessionIdentity.sanitise(product.id) == product.id else {
            throw NSError(domain: "SattariHub", code: 18, userInfo: [NSLocalizedDescriptionKey: "Invalid package paths. Nothing was installed."])
        }
        if !productIsAvailable(product) {
            try prepareRemotePayloadIfAvailable(for: product, control: control)
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
            try control.checkpoint("Staging \(product.name) — \(bundle.kind) (\(index + 1)/\(product.bundles.count))…")
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
            .replacingOccurrences(of: ":", with: "-") + "-" + UUID().uuidString
        var committed: [CommittedBundle] = []
        let previousReceipts = receipts

        do {
            for (index, item) in prepared.enumerated() {
                try control.checkpoint("Installing \(product.name) — \(item.bundle.kind) (\(index + 1)/\(prepared.count))…")
                try fileManager.createDirectory(
                    at: item.destination.deletingLastPathComponent(),
                    withIntermediateDirectories: true)

                var backup: URL?
                if fileManager.fileExists(atPath: item.destination.path) {
                    let candidate = backupRoot.appendingPathComponent(
                        "\(stamp)-\(index)-\(item.destination.lastPathComponent)")
                    try fileManager.moveItem(at: item.destination, to: candidate)
                    backup = candidate
                }

                // Record the in-flight item too, so one rollback path owns all
                // restoration and can report a secondary filesystem failure.
                committed.append(CommittedBundle(destination: item.destination, backup: backup))
                try fileManager.moveItem(at: item.staged, to: item.destination)
                ensureInstalledPermissions(at: item.destination)
                guard bundleIsUsable(at: item.destination, bundle: item.bundle) else {
                    throw NSError(domain: "SattariHub", code: 4, userInfo: [
                        NSLocalizedDescriptionKey:
                            "Installed \(item.bundle.kind) bundle failed post-copy validation."
                    ])
                }
            }
            // A successful receipt is part of the install transaction, not an
            // afterthought. A full/read-only disk must roll bundles back too.
            try control.checkpoint("Saving installation receipt for \(product.name)…")
            receipts[product.id] = InstallReceipt(
                productId: product.id, version: product.version,
                installedAt: ISO8601DateFormatter().string(from: Date()),
                bundleCount: prepared.count, updateChannel: manifest.defaultUpdateChannel)
            try saveReceipts()
        } catch {
            receipts = previousReceipts
            var recoveryFailures: [String] = []
            for item in committed.reversed() {
                do {
                    if fileManager.fileExists(atPath: item.destination.path) {
                        try fileManager.removeItem(at: item.destination)
                    }
                    if let backup = item.backup {
                        try fileManager.moveItem(at: backup, to: item.destination)
                    }
                } catch { recoveryFailures.append("\(item.destination.lastPathComponent): \(error.localizedDescription)") }
            }
            if !recoveryFailures.isEmpty {
                throw NSError(domain: "SattariHub", code: 21, userInfo: [
                    NSUnderlyingErrorKey: error,
                    NSLocalizedDescriptionKey: "Installation did not complete. Recovery needs attention: \(recoveryFailures.joined(separator: "; ")). Recovery backups: \(backupRoot.path)"
                ])
            }
            throw error
        }

        let superseded = retireSupersededBundles(
            installed: committed.map(\.destination), backupRoot: backupRoot, stamp: stamp)

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

    func prepareRemotePayloadIfAvailable(for product: HubProduct, control: InstallControl = InstallControl()) throws {
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

        let archivePath = downloadsDir.appendingPathComponent(UUID().uuidString + ".tar.gz")
        defer { try? fileManager.removeItem(at: archivePath) }
        let payloadRoot = payloadsDir.appendingPathComponent(
            "\(archiveCacheName(for: archiveURL))-\(expectedHash.prefix(12))",
            isDirectory: true)

        if !fileManager.fileExists(atPath: payloadRoot.path) {
            try control.checkpoint("Downloading \(product.name)…")
            try downloadArchive(from: archiveURL, to: archivePath, control: control)
            let size = try archivePath.resourceValues(forKeys: [.fileSizeKey]).fileSize
            guard size.map(Int64.init) == expectedBytes else {
                throw NSError(domain: "SattariHub", code: 6, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Downloaded archive size mismatch for \(product.name)."
                ])
            }
            try control.checkpoint("Verifying \(product.name)…")
            let stream = try FileHandle(forReadingFrom: archivePath)
            defer { try? stream.close() }
            var digest = SHA256()
            while let chunk = try stream.read(upToCount: 1024 * 1024), !chunk.isEmpty {
                try control.checkCancellation()
                digest.update(data: chunk)
            }
            guard digest.finalize().map({ String(format: "%02x", $0) }).joined() == expectedHash.lowercased() else {
                throw NSError(domain: "SattariHub", code: 7, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Downloaded archive checksum mismatch for \(product.name)."
                ])
            }
            try? fileManager.removeItem(at: payloadRoot)
            try fileManager.createDirectory(at: payloadRoot, withIntermediateDirectories: true)
            var extracted = false
            defer { if !extracted { try? fileManager.removeItem(at: payloadRoot) } }

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

            try control.checkpoint("Extracting \(product.name)…")
            let (extractStatus, _) = try runToolCapturingOutput(
                "/usr/bin/tar", ["-xzf", archivePath.path, "-C", payloadRoot.path])
            if extractStatus != 0 {
                try? fileManager.removeItem(at: payloadRoot)
                throw NSError(domain: "SattariHub", code: 11, userInfo: [
                    NSLocalizedDescriptionKey:
                        "Could not extract update archive for \(product.name)."
                ])
            }
            try control.checkCancellation()
            extracted = true
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

final class HubWindowController: NSWindowController, NSWindowDelegate {
    private let model: HubModel
    private let stack = NSStackView()
    private let statusLabel = NSTextField(labelWithString: "")
    private let sessionField = NSTextField(string: SessionIdentity.current())
    private let dawMenu = NSPopUpButton()
    private var dawURLs: [URL] = []
    private var expandedSuites: Set<String> = []
    private let installQueue = DispatchQueue(label: "com.sattari.hub.install", qos: .userInitiated)
    private var activeInstall: InstallControl?
    private let installProgress = NSProgressIndicator()
    private let cancelInstallButton = NSButton(title: "Cancel", target: nil, action: nil)
    private var disabledControls: [(NSControl, Bool)] = []

    var isInstalling: Bool { activeInstall != nil }

    init(model: HubModel) {
        self.model = model
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 980, height: 800),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Sattari Hub"
        window.center()
        super.init(window: window)
        window.delegate = self
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

        let onlineUpdates = model.manifest.products.contains { model.productCanDownloadUpdate($0) }
        let mode = onlineUpdates ? "Installer + updater hub" : "Offline installer + local updates"
        let subtitle = NSTextField(labelWithString: "\(mode) • \(model.manifest.suiteVersion) • \(model.manifest.defaultUpdateChannel)")
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
        statusLabel.maximumNumberOfLines = 4
        statusLabel.lineBreakMode = .byWordWrapping
        let bundledCount = model.manifest.products.filter { model.productIsAvailable($0) }.count
        statusLabel.stringValue = model.repoRoot == nil
            ? "Bundled package mode: \(bundledCount) product(s) available inside this Hub."
            : "Package root: \(model.repoRoot!.path) • bundled/local available products: \(bundledCount)"
        root.addArrangedSubview(statusLabel)

        let sessionBox = NSBox()
        sessionBox.boxType = .custom
        sessionBox.cornerRadius = 12
        sessionBox.borderWidth = 1
        sessionBox.borderColor = NSColor(calibratedRed: 0.2, green: 0.95, blue: 0.82, alpha: 0.28)
        sessionBox.fillColor = NSColor(calibratedRed: 0.055, green: 0.07, blue: 0.105, alpha: 1)
        let sessionRow = NSStackView()
        sessionRow.orientation = .horizontal
        sessionRow.alignment = .centerY
        sessionRow.spacing = 10
        sessionRow.edgeInsets = NSEdgeInsets(top: 10, left: 12, bottom: 10, right: 12)
        sessionRow.translatesAutoresizingMaskIntoConstraints = false
        sessionBox.contentView?.addSubview(sessionRow)
        NSLayoutConstraint.activate([
            sessionRow.leadingAnchor.constraint(equalTo: sessionBox.contentView!.leadingAnchor),
            sessionRow.trailingAnchor.constraint(equalTo: sessionBox.contentView!.trailingAnchor),
            sessionRow.topAnchor.constraint(equalTo: sessionBox.contentView!.topAnchor),
            sessionRow.bottomAnchor.constraint(equalTo: sessionBox.contentView!.bottomAnchor)
        ])

        let sessionLabel = NSTextField(labelWithString: "Project session")
        sessionLabel.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        sessionLabel.textColor = .white
        sessionField.placeholderString = "album-or-project-name"
        sessionField.toolTip = "Shared Session Bus identity for this DAW launch"
        sessionField.widthAnchor.constraint(equalToConstant: 190).isActive = true
        let regenerate = NSButton(title: "New ID", target: self, action: #selector(regenerateSessionClicked(_:)))
        regenerate.bezelStyle = .rounded
        dawURLs = SessionIdentity.installedDAWs()
        dawMenu.addItems(withTitles: dawURLs.map { $0.deletingPathExtension().lastPathComponent })
        dawMenu.toolTip = "DAW to launch with this isolated project session"
        let launch = NSButton(title: "Launch isolated DAW", target: self, action: #selector(launchDAWClicked(_:)))
        launch.bezelStyle = .rounded
        launch.isEnabled = !dawURLs.isEmpty
        sessionRow.addArrangedSubview(sessionLabel)
        sessionRow.addArrangedSubview(sessionField)
        sessionRow.addArrangedSubview(regenerate)
        sessionRow.addArrangedSubview(dawMenu)
        sessionRow.addArrangedSubview(launch)
        root.addArrangedSubview(sessionBox)

        // Batch actions are limited to the explicitly curated alpha profile. A
        // 48-plugin "Install All" makes unvalidated experiments look shippable.
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
        installProgress.style = .spinning
        installProgress.controlSize = .small
        installProgress.isHidden = true
        cancelInstallButton.target = self
        cancelInstallButton.action = #selector(cancelInstallClicked(_:))
        cancelInstallButton.bezelStyle = .rounded
        cancelInstallButton.isHidden = true
        cancelInstallButton.toolTip = "Cancel downloads immediately; finish the current file operation and restore any in-progress installation. Completed products remain installed."
        actionRow.addArrangedSubview(installProgress)
        actionRow.addArrangedSubview(cancelInstallButton)
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

        for suite in model.manifest.suite {
            let members = model.manifest.products.filter { $0.suite == suite.id }
            stack.addArrangedSubview(card(for: suite, members: members))
        }
    }

    private static func accentColor(_ name: String) -> NSColor {
        switch name {
        case "blue": return NSColor(calibratedRed: 0.41, green: 0.65, blue: 1.0, alpha: 1)
        case "violet": return NSColor(calibratedRed: 0.61, green: 0.48, blue: 1.0, alpha: 1)
        case "amber": return NSColor(calibratedRed: 0.96, green: 0.64, blue: 0.29, alpha: 1)
        case "crimson": return NSColor(calibratedRed: 0.91, green: 0.22, blue: 0.31, alpha: 1)
        default: return NSColor(calibratedRed: 0.38, green: 0.96, blue: 0.78, alpha: 1)
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

    private func card(for suite: HubSuite, members: [HubProduct]) -> NSView {
        let accent = Self.accentColor(suite.accent)
        let shipping = members.filter { $0.shippingProfile != nil }
        let primary = members.first { $0.id == suite.primary }

        let box = NSBox()
        box.boxType = .custom
        box.cornerRadius = 16
        box.borderWidth = 1
        box.borderColor = accent.withAlphaComponent(0.34)
        box.fillColor = NSColor(calibratedRed: 0.07, green: 0.09, blue: 0.13, alpha: 1)

        let content = NSStackView()
        content.orientation = .vertical
        content.spacing = 12
        content.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        content.translatesAutoresizingMaskIntoConstraints = false
        box.contentView?.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: box.contentView!.leadingAnchor),
            content.trailingAnchor.constraint(equalTo: box.contentView!.trailingAnchor),
            content.topAnchor.constraint(equalTo: box.contentView!.topAnchor),
            content.bottomAnchor.constraint(equalTo: box.contentView!.bottomAnchor)
        ])

        let header = NSStackView()
        header.orientation = .horizontal
        header.alignment = .centerY
        header.spacing = 16
        content.addArrangedSubview(header)

        let accentBar = NSView()
        accentBar.wantsLayer = true
        accentBar.layer?.backgroundColor = accent.cgColor
        accentBar.layer?.cornerRadius = 2.5
        accentBar.translatesAutoresizingMaskIntoConstraints = false
        accentBar.widthAnchor.constraint(equalToConstant: 5).isActive = true
        accentBar.heightAnchor.constraint(equalToConstant: 62).isActive = true
        header.addArrangedSubview(accentBar)

        let copy = NSStackView()
        copy.orientation = .vertical
        copy.spacing = 5
        header.addArrangedSubview(copy)

        let name = NSTextField(labelWithString: suite.name)
        name.font = NSFont.boldSystemFont(ofSize: 20)
        name.textColor = .white
        copy.addArrangedSubview(name)

        let meta = NSTextField(labelWithString:
            "\(suite.verb.uppercased())  •  \(suite.tier.uppercased())  •  \(members.count) PLUGINS")
        meta.font = NSFont.systemFont(ofSize: 10, weight: .bold)
        meta.textColor = accent
        copy.addArrangedSubview(meta)

        let tagline = NSTextField(wrappingLabelWithString: suite.tagline)
        tagline.font = NSFont.systemFont(ofSize: 12)
        tagline.textColor = NSColor(calibratedWhite: 0.75, alpha: 1)
        copy.addArrangedSubview(tagline)

        let disclosure = NSButton(
            title: expandedSuites.contains(suite.id) ? "Hide components" : "Components",
            target: self,
            action: #selector(toggleSuiteClicked(_:)))
        disclosure.identifier = NSUserInterfaceItemIdentifier(suite.id)
        disclosure.bezelStyle = .rounded
        header.addArrangedSubview(disclosure)

        let install = NSButton(
            title: shipping.isEmpty ? "Preview" : (shipping.count == 1 ? "Install alpha" : "Install alpha set"),
            target: self,
            action: #selector(installSuiteClicked(_:)))
        install.identifier = NSUserInterfaceItemIdentifier(suite.id)
        install.bezelStyle = .rounded
        install.isEnabled = !shipping.isEmpty && shipping.allSatisfy {
            model.productIsAvailable($0) || model.productCanDownloadUpdate($0)
        }
        install.toolTip = shipping.isEmpty
            ? "This suite is a preview and has no public install profile."
            : "Installs the configured alpha products in this suite. Alpha availability is not public-release approval."
        header.addArrangedSubview(install)

        let state = NSTextField(labelWithString: primary.map { model.statusText(for: $0) }
            ?? "Primary product metadata is unavailable.")
        state.font = NSFont.systemFont(ofSize: 11, weight: .medium)
        state.textColor = NSColor(calibratedWhite: 0.58, alpha: 1)
        content.addArrangedSubview(state)

        if expandedSuites.contains(suite.id) {
            let separator = NSBox()
            separator.boxType = .separator
            content.addArrangedSubview(separator)
            for product in members.sorted(by: componentOrder) {
                content.addArrangedSubview(componentRow(for: product, accent: accent))
            }
        }

        return box
    }

    private func componentOrder(_ left: HubProduct, _ right: HubProduct) -> Bool {
        if left.suiteRole != right.suiteRole { return left.suiteRole == "primary" }
        return left.name.localizedCaseInsensitiveCompare(right.name) == .orderedAscending
    }

    private func componentRow(for product: HubProduct, accent: NSColor) -> NSView {
        let row = NSStackView()
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 12
        row.edgeInsets = NSEdgeInsets(top: 4, left: 10, bottom: 4, right: 4)

        let role = product.suiteRole == "primary" ? "PRIMARY" : "COMPONENT"
        let name = NSTextField(labelWithString: product.name)
        name.font = NSFont.systemFont(ofSize: 13, weight: product.suiteRole == "primary" ? .semibold : .regular)
        name.textColor = .white
        name.setContentHuggingPriority(.defaultLow, for: .horizontal)
        row.addArrangedSubview(name)

        let meta = NSTextField(labelWithString:
            "\(role)  •  \((product.lifecycle ?? product.status).uppercased())  •  \(product.formats.joined(separator: "/"))")
        meta.font = NSFont.monospacedSystemFont(ofSize: 10, weight: .medium)
        meta.textColor = product.shippingProfile == nil
            ? NSColor(calibratedWhite: 0.5, alpha: 1)
            : accent
        row.addArrangedSubview(meta)

        let action = NSButton(
            title: product.shippingProfile == nil
                ? "Not shipping"
                : (model.productNeedsUpdate(product) ? "Install" : "Reinstall"),
            target: self,
            action: #selector(installClicked(_:)))
        action.identifier = NSUserInterfaceItemIdentifier(product.id)
        action.bezelStyle = .rounded
        action.isEnabled = product.shippingProfile != nil
            && (model.productIsAvailable(product) || model.productCanDownloadUpdate(product))
        row.addArrangedSubview(action)
        return row
    }

    @objc private func installClicked(_ sender: NSButton) {
        guard let productId = sender.identifier?.rawValue,
              let product = model.manifest.products.first(where: { $0.id == productId }) else { return }
        installMany(products: [product], label: product.name)
    }

    @objc private func toggleSuiteClicked(_ sender: NSButton) {
        guard let suiteId = sender.identifier?.rawValue else { return }
        if expandedSuites.contains(suiteId) {
            expandedSuites.remove(suiteId)
        } else {
            expandedSuites.insert(suiteId)
        }
        reloadProductCards()
    }

    @objc private func installSuiteClicked(_ sender: NSButton) {
        guard let suiteId = sender.identifier?.rawValue,
              let suite = model.manifest.suite.first(where: { $0.id == suiteId }) else { return }
        let products = model.manifest.products.filter {
            $0.suite == suiteId && $0.shippingProfile != nil
        }
        guard !products.isEmpty else {
            statusLabel.stringValue = "\(suite.name) is still a preview; no install profile is published."
            return
        }
        installMany(products: products, label: suite.name)
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

    @objc private func regenerateSessionClicked(_ sender: NSButton) {
        let generated = "project-" + String(UUID().uuidString.lowercased().prefix(8))
        sessionField.stringValue = SessionIdentity.save(generated)
        statusLabel.stringValue = "New isolated project identity created. Launch the DAW from Hub to use it."
    }

    @objc private func launchDAWClicked(_ sender: NSButton) {
        let index = dawMenu.indexOfSelectedItem
        guard dawURLs.indices.contains(index) else {
            statusLabel.stringValue = "No supported DAW was found in Applications."
            return
        }
        do {
            let sessionId = SessionIdentity.save(sessionField.stringValue)
            sessionField.stringValue = sessionId
            try SessionIdentity.launch(dawURLs[index], sessionId: sessionId)
            statusLabel.stringValue = "Launched \(dawMenu.titleOfSelectedItem ?? "DAW") with isolated session \(sessionId)."
        } catch {
            statusLabel.stringValue = "DAW launch failed: \(error.localizedDescription)"
            NSSound.beep()
        }
    }

    private func installMany(products: [HubProduct], label: String) {
        guard !isInstalling else { return }
        let configuration = model.installConfiguration
        let control = InstallControl { [weak self] message in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.activeInstall?.isCancelled == false else { return }
                self.statusLabel.stringValue = message
            }
        }
        activeInstall = control
        setInstallBusy(true)
        statusLabel.stringValue = "\(label): preparing \(products.count) product(s)…"
        // This model belongs exclusively to the serial worker. AppKit keeps its
        // own model and reloads receipts only after the worker has finished.
        installQueue.async { [weak self] in
            var installed = 0
            var failed: [String] = []
            do {
                let worker = try HubModel(manifest: configuration.manifest,
                    storageRoot: configuration.storageRoot, installationRoot: configuration.installationRoot,
                    sourceRoot: configuration.sourceRoot)
                for product in products {
                    if control.isCancelled { break }
                    do {
                        _ = try worker.install(product: product, control: control)
                        installed += 1
                    } catch {
                        // Do not hide a failed rollback behind a cancellation.
                        if !control.isCancelled || (error as NSError).code != NSUserCancelledError {
                            failed.append("\(product.name) (\(error.localizedDescription))")
                        }
                        if control.isCancelled || ((error as NSError).domain == "SattariHub" && (error as NSError).code == 21) { break }
                    }
                }
            } catch { failed.append(error.localizedDescription) }
            let message = "\(label): \(installed)/\(products.count) installed. "
                + (control.isCancelled ? "Cancelled; completed products remain installed. "
                    : (failed.isEmpty ? "Restart or rescan your DAW. " : "Review the recovery details before opening your DAW. "))
                + (failed.isEmpty ? "" : "Needs attention: \(failed.joined(separator: "; "))")
            let hasFailures = !failed.isEmpty
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.activeInstall = nil
                self.model.reloadReceipts()
                self.setInstallBusy(false)
                self.statusLabel.stringValue = message
                self.statusLabel.toolTip = message
                self.reloadProductCards()
                if hasFailures { NSSound.beep() }
            }
        }
    }

    private func setInstallBusy(_ busy: Bool) {
        installProgress.isHidden = !busy
        cancelInstallButton.isHidden = !busy
        cancelInstallButton.isEnabled = busy
        if busy {
            installProgress.startAnimation(nil)
            func disable(_ view: NSView) {
                if let control = view as? NSControl, control !== cancelInstallButton,
                   control is NSButton || control === sessionField || control === dawMenu {
                    disabledControls.append((control, control.isEnabled))
                    control.isEnabled = false
                }
                view.subviews.forEach(disable)
            }
            if let view = window?.contentView { disable(view) }
        } else {
            installProgress.stopAnimation(nil)
            disabledControls.forEach { $0.0.isEnabled = $0.1 }
            disabledControls.removeAll()
        }
    }

    @objc private func cancelInstallClicked(_ sender: NSButton) { requestSafeCancellation() }

    func requestSafeCancellation() {
        activeInstall?.cancel()
        cancelInstallButton.isEnabled = false
        statusLabel.stringValue = "Cancelling safely after the current file operation. Please keep Hub open while recovery finishes…"
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if isInstalling { requestSafeCancellation(); return false }
        return true
    }
}

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    private var controller: HubWindowController?
    private var loadingWindow: NSWindow?

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if controller?.isInstalling == true {
            controller?.requestSafeCancellation()
            return .terminateCancel
        }
        return .terminateNow
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let loading = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 460, height: 120),
            styleMask: [.titled, .closable], backing: .buffered, defer: false)
        loading.title = "Sattari Hub"
        let label = NSTextField(labelWithString: "Loading product catalog…")
        label.frame = NSRect(x: 24, y: 45, width: 410, height: 24)
        loading.contentView?.addSubview(label)
        loading.center()
        loading.makeKeyAndOrderFront(nil)
        loadingWindow = loading
        NSApp.activate(ignoringOtherApps: true)
        // Remote catalog lookup must never stall AppKit's launch event loop.
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let result = Result { try HubModel.loadManifest() }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                do {
                    self.controller = HubWindowController(model: try HubModel(manifest: result.get()))
                    self.controller?.showWindow(nil)
                    self.loadingWindow?.orderOut(nil)
                    self.loadingWindow = nil
                } catch {
                    NSAlert(error: error).runModal()
                    NSApp.terminate(nil)
                }
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}

if CommandLine.arguments.contains("--contract-check") {
    do {
        guard SessionIdentity.sanitise(" Album A! / 2026 ") == "AlbumA2026",
              SessionIdentity.sanitise("Música-東京_１２3") == "Msica-_3",
              SessionIdentity.sanitise(String(repeating: "a", count: 80)).count == 48 else {
            throw NSError(domain: "SattariHub", code: 9, userInfo: [NSLocalizedDescriptionKey: "Session identity contract failed"])
        }
        let model = try HubModel()
        try runReleaseSafetyContract()
        let productCount = model.manifest.products.count
        let bundleCount = model.manifest.products.reduce(0) { $0 + $1.bundles.count }
        guard model.manifest.suite.count == 6 else { throw NSError(domain: "SattariHub", code: 10, userInfo: [NSLocalizedDescriptionKey: "Expected exactly 6 suite products, got \(model.manifest.suite.count)"]) }
        guard productCount >= 6 else { throw NSError(domain: "SattariHub", code: 10, userInfo: [NSLocalizedDescriptionKey: "Expected at least 6 products, got \(productCount)"]) }
        guard model.manifest.products.contains(where: { $0.id == "vocal" }) else { throw NSError(domain: "SattariHub", code: 11, userInfo: [NSLocalizedDescriptionKey: "Missing Vocal product"]) }
        for suite in model.manifest.suite {
            let members = model.manifest.products.filter { $0.suite == suite.id }
            guard !members.isEmpty,
                  members.filter({ $0.suiteRole == "primary" }).map(\.id) == [suite.primary] else {
                throw NSError(domain: "SattariHub", code: 16, userInfo: [NSLocalizedDescriptionKey: "Suite grouping contract failed for \(suite.id)"])
            }
        }
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
        print("SattariHub contract ok: suites=\(model.manifest.suite.count) products=\(productCount) bundles=\(bundleCount) availableProducts=\(available) downloadableProducts=\(downloadable) channel=\(model.manifest.defaultUpdateChannel)")
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

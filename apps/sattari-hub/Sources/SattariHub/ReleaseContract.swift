import Foundation
import AppKit

/// Installer integration checks only use owned fixture directories. No network,
/// real plug-in installation, signing, or user receipts are involved.
@MainActor func runReleaseSafetyContract() throws {
    let fm = FileManager.default
    var checks = 0
    func require(_ condition: Bool, _ message: String) throws {
        checks += 1
        if !condition {
            throw NSError(domain: "SattariHubContract", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: message])
        }
    }
    let root = fm.temporaryDirectory.appendingPathComponent("sattari-hub-contract-" + UUID().uuidString)
    try fm.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? fm.removeItem(at: root) }
    let sources = root.appendingPathComponent("sources")
    let storage = root.appendingPathComponent("storage")
    let destination = root.appendingPathComponent("destination")
    let bundles = [
        InstallBundle(kind: "AU", relativeSourcePath: "build/Fixture.component",
                      installSubpath: "Library/Audio/Plug-Ins/Components/Fixture.component"),
        InstallBundle(kind: "VST3", relativeSourcePath: "build/Fixture.vst3",
                      installSubpath: "Library/Audio/Plug-Ins/VST3/Fixture.vst3")
    ]
    for bundle in bundles { try require(bundle.hasSafePaths, "valid bundle rejected") }
    for bundle in [
        InstallBundle(kind: "AU", relativeSourcePath: "../Fixture.component", installSubpath: bundles[0].installSubpath),
        InstallBundle(kind: "AU", relativeSourcePath: "/tmp/Fixture.component", installSubpath: bundles[0].installSubpath),
        InstallBundle(kind: "AU", relativeSourcePath: bundles[0].relativeSourcePath, installSubpath: "Library/Audio/Plug-Ins/Components/../Fixture.component"),
        InstallBundle(kind: "AU", relativeSourcePath: bundles[0].relativeSourcePath, installSubpath: "Documents/Fixture.component"),
        InstallBundle(kind: "VST3", relativeSourcePath: bundles[1].relativeSourcePath, installSubpath: bundles[0].installSubpath)
    ] { try require(!bundle.hasSafePaths, "unsafe destination accepted") }
    let product = HubProduct(id: "fixture", name: "Fixture", category: "Test", accent: nil,
                             version: "1", status: "test", description: "Fixture only", formats: ["AU", "VST3"],
                             bundles: bundles, updateArchiveURL: nil, updateArchiveSHA256: nil,
                             updateArchiveBytes: nil, lifecycle: nil, shippingProfile: nil, suite: nil, suiteRole: nil)
    let manifest = HubManifest(schemaVersion: 1, suiteName: "Fixture", suiteVersion: "1", truthLabel: "Test",
                               defaultUpdateChannel: "test", updateFeedURL: nil, updateFeedSHA256: nil,
                               suite: [], products: [product])
    let model = try HubModel(manifest: manifest, storageRoot: storage,
                             installationRoot: destination, sourceRoot: sources)
    func fixtureBundle(_ url: URL, marker: String) throws {
        let executable = url.appendingPathComponent("Contents/MacOS/Fixture")
        try fm.createDirectory(at: executable.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("fixture, not an executable product".utf8).write(to: executable)
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: executable.path)
        let plist = try PropertyListSerialization.data(fromPropertyList: ["CFBundleName": "Fixture"], format: .xml, options: 0)
        try plist.write(to: url.appendingPathComponent("Contents/Info.plist"))
        try Data(marker.utf8).write(to: url.appendingPathComponent("marker"))
    }
    // Missing payload must fail before touching any existing install.
    var rejected = false
    do { _ = try model.install(product: product) } catch { rejected = true }
    try require(rejected, "missing payload should fail")
    try require(!fm.fileExists(atPath: destination.path), "missing payload modified installation")
    for bundle in bundles {
        try fixtureBundle(sources.appendingPathComponent(bundle.relativeSourcePath), marker: "new")
        try fixtureBundle(destination.appendingPathComponent(bundle.installSubpath), marker: "old")
    }
    // A directory at receipts.json deterministically simulates a write failure,
    // without depending on uid or touching actual user permissions.
    let receipt = storage.appendingPathComponent("receipts.json")
    try fm.createDirectory(at: receipt, withIntermediateDirectories: true)
    rejected = false
    do { _ = try model.install(product: product) } catch { rejected = true }
    try require(rejected, "receipt error should fail install")
    try require(model.receipts.isEmpty, "receipt failure changed in-memory installation state")
    for bundle in bundles {
        let marker = try String(contentsOf: destination.appendingPathComponent(bundle.installSubpath).appendingPathComponent("marker"), encoding: .utf8)
        try require(marker == "old", "receipt failure did not restore the old bundle")
    }
    try fm.removeItem(at: receipt)
    for _ in 0..<3 { _ = try model.install(product: product) }
    try require(model.receipts[product.id]?.version == "1", "successful install missing receipt")
    for bundle in bundles {
        let marker = try String(contentsOf: destination.appendingPathComponent(bundle.installSubpath).appendingPathComponent("marker"), encoding: .utf8)
        try require(marker == "new", "successful install left old bundle")
    }
    let backups = try fm.contentsOfDirectory(atPath: storage.appendingPathComponent("Backups/fixture").path)
    try require(backups.count == 6, "rapid installs replaced earlier recovery backups")
    let reopened = try HubModel(manifest: manifest, storageRoot: storage,
                                installationRoot: destination, sourceRoot: sources)
    try require(reopened.receipts[product.id]?.bundleCount == 2, "relaunch lost install receipt")
    let staging = try fm.contentsOfDirectory(atPath: storage.appendingPathComponent("InstallStaging").path)
    try require(staging.isEmpty, "installation left staged payloads behind")

    // Cancellation on a different thread must stop staging or roll back a
    // partially/fully copied product without altering its previous receipt.
    for bundle in bundles {
        try Data("candidate".utf8).write(to: sources.appendingPathComponent(bundle.relativeSourcePath).appendingPathComponent("marker"))
    }
    let receiptBeforeCancel = try Data(contentsOf: receipt)
    for stage in ["Staging Fixture — VST3", "Installing Fixture — VST3", "Saving installation receipt"] {
        let reached = DispatchSemaphore(value: 0)
        let resume = DispatchSemaphore(value: 0)
        let control = InstallControl { message in
            if message.hasPrefix(stage) { reached.signal(); resume.wait() }
        }
        DispatchQueue.global().async {
            if reached.wait(timeout: .now() + 10) == .success { control.cancel() }
            resume.signal()
        }
        var cancelled = false
        do { _ = try model.install(product: product, control: control) }
        catch { cancelled = (error as NSError).code == NSUserCancelledError }
        try require(cancelled, "cancellation was not reported at \(stage)")
        for bundle in bundles {
            let marker = try String(contentsOf: destination.appendingPathComponent(bundle.installSubpath).appendingPathComponent("marker"), encoding: .utf8)
            try require(marker == "new", "cancellation left a partially updated bundle at \(stage)")
        }
        let receiptAfterCancel = try Data(contentsOf: receipt)
        try require(receiptAfterCancel == receiptBeforeCancel, "cancellation changed the receipt")
        let leftover = try fm.contentsOfDirectory(atPath: storage.appendingPathComponent("InstallStaging").path)
        try require(leftover.isEmpty, "cancellation left staging files")
    }
    let cancelledBeforeStart = InstallControl()
    cancelledBeforeStart.cancel()
    rejected = false
    do { _ = try model.install(product: product, control: cancelledBeforeStart) } catch { rejected = true }
    try require(rejected, "pre-cancelled operation modified the install")
    var insecureRejected = false
    do {
        try downloadArchive(from: URL(string: "http://invalid.example/archive.tar.gz")!,
                            to: root.appendingPathComponent("never-download"), control: InstallControl())
    } catch { insecureRejected = (error as NSError).code == 19 }
    try require(insecureRejected, "insecure update URL was not rejected before network access")

    // Exercise the real AppKit button/controller path against fixture roots.
    // No user plug-in folders or receipts are involved.
    _ = NSApplication.shared
    let uiProduct = HubProduct(id: product.id, name: product.name, category: product.category,
        accent: nil, version: "2", status: "test", description: "Fixture only",
        formats: product.formats, bundles: product.bundles, updateArchiveURL: nil,
        updateArchiveSHA256: nil, updateArchiveBytes: nil, lifecycle: "test",
        shippingProfile: "alpha", suite: "fixture", suiteRole: "primary")
    let uiManifest = HubManifest(schemaVersion: 1, suiteName: "Fixture", suiteVersion: "1",
        truthLabel: "Fixture only", defaultUpdateChannel: "test", updateFeedURL: nil, updateFeedSHA256: nil,
        suite: [HubSuite(id: "fixture", name: "Fixture", verb: "Test", accent: "blue",
                        tier: "test", tagline: "Fixture only", primary: "fixture")], products: [uiProduct])
    let uiModel = try HubModel(manifest: uiManifest, storageRoot: storage,
                              installationRoot: destination, sourceRoot: sources)
    let controller = HubWindowController(model: uiModel)
    func button(_ title: String, in view: NSView) -> NSButton? {
        if let candidate = view as? NSButton, candidate.title == title { return candidate }
        for child in view.subviews { if let found = button(title, in: child) { return found } }
        return nil
    }
    guard let window = controller.window, let view = window.contentView,
          let install = button("Install alpha", in: view), let action = install.action,
          let cancel = button("Cancel", in: view) else {
        throw NSError(domain: "SattariHubContract", code: 2,
                      userInfo: [NSLocalizedDescriptionKey: "Installer UI controls not found"])
    }
    NSApp.sendAction(action, to: install.target, from: install)
    try require(controller.isInstalling && !install.isEnabled, "install action did not enter a guarded busy state")
    try require(!cancel.isHidden && cancel.isEnabled, "busy UI has no accessible cancel action")
    NSApp.sendAction(action, to: install.target, from: install) // re-entry must be ignored
    try require(!controller.windowShouldClose(window), "window closed while a transaction was active")
    let deadline = Date().addingTimeInterval(15)
    while controller.isInstalling && Date() < deadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.01))
    }
    try require(!controller.isInstalling, "asynchronous install/cancel did not finish")
    try require(cancel.isHidden && controller.windowShouldClose(window), "UI did not recover after cancellation")
    try require(uiModel.receipts[product.id]?.version == "1", "cancelled UI update changed the installed version")

    // Complete an update through the same real button path. The UI model must
    // refresh the receipt produced by its separately owned worker model.
    guard let retry = button("Install alpha", in: view), let retryAction = retry.action else {
        throw NSError(domain: "SattariHubContract", code: 3,
                      userInfo: [NSLocalizedDescriptionKey: "Install control missing after cancellation"])
    }
    NSApp.sendAction(retryAction, to: retry.target, from: retry)
    try require(controller.isInstalling, "retry did not start an asynchronous update")
    let completionDeadline = Date().addingTimeInterval(15)
    while controller.isInstalling && Date() < completionDeadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.01))
    }
    try require(!controller.isInstalling, "successful UI update did not complete")
    try require(uiModel.receipts[product.id]?.version == "2", "UI did not reload the worker's updated receipt")
    for bundle in bundles {
        let marker = try String(contentsOf: destination.appendingPathComponent(bundle.installSubpath).appendingPathComponent("marker"), encoding: .utf8)
        try require(marker == "candidate", "successful UI update did not replace every fixture bundle")
    }
    try require(button("Install alpha", in: view)?.isEnabled == true && cancel.isHidden,
                "successful UI update did not restore its controls")
    window.close()

    // Simulate losing access to an installation parent after commit began.
    // Recovery must report the retained backup location, not silently fail.
    let componentParent = destination.appendingPathComponent(bundles[0].installSubpath).deletingLastPathComponent()
    let displacedParent = root.appendingPathComponent("displaced-components")
    let recoveryReached = DispatchSemaphore(value: 0)
    let recoveryResume = DispatchSemaphore(value: 0)
    let recoveryControl = InstallControl { message in
        if message.hasPrefix("Saving installation receipt") {
            try? FileManager.default.moveItem(at: componentParent, to: displacedParent)
            try? Data("blocked parent".utf8).write(to: componentParent)
            recoveryReached.signal()
            recoveryResume.wait()
        }
    }
    DispatchQueue.global().async {
        if recoveryReached.wait(timeout: .now() + 10) == .success { recoveryControl.cancel() }
        recoveryResume.signal()
    }
    var recoveryMessage = ""
    do { _ = try model.install(product: product, control: recoveryControl) }
    catch { recoveryMessage = error.localizedDescription }
    try require(recoveryMessage.contains("Recovery needs attention"), "secondary rollback failure was hidden")
    try require(recoveryMessage.contains(storage.appendingPathComponent("Backups/fixture").path), "recovery error omitted backup location")
    let remainingBackups = try fm.contentsOfDirectory(atPath: storage.appendingPathComponent("Backups/fixture").path)
    try require(remainingBackups.contains(where: { $0.hasSuffix("Fixture.component") }), "failed recovery discarded backup")
    print("Hub release safety contract: \(checks) checks passed (isolated fixture bundles)")
}

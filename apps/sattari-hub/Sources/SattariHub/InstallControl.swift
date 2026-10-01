import Foundation

/// Cancellation is checked before staging and between transactional mutations.
/// A file copy already in progress finishes before rollback; downloads cancel
/// their URLSession task. All mutable cross-thread state is protected by a lock.
final class InstallControl: @unchecked Sendable {
    private let lock = NSLock()
    private var cancelled = false
    private var cancelHandler: (@Sendable () -> Void)?
    private let progress: @Sendable (String) -> Void

    init(progress: @escaping @Sendable (String) -> Void = { _ in }) {
        self.progress = progress
    }

    var isCancelled: Bool {
        lock.lock(); defer { lock.unlock() }
        return cancelled
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let handler = cancelHandler
        lock.unlock()
        handler?()
    }

    func onCancel(_ handler: (@Sendable () -> Void)?) {
        lock.lock()
        cancelHandler = handler
        let invoke = cancelled
        lock.unlock()
        if invoke { handler?() }
    }

    func checkpoint(_ message: String) throws {
        progress(message)
        try checkCancellation()
    }

    func checkCancellation() throws {
        if isCancelled { throw CocoaError(.userCancelled) }
    }
}

/// The worker waits for completion, never the AppKit thread. URLSession owns its
/// temporary download until completion, so move it before returning the callback.
private final class DownloadCompletion: @unchecked Sendable {
    let done = DispatchSemaphore(value: 0)
    private let lock = NSLock()
    private var failure: Error?
    func finish(_ error: Error?) {
        lock.lock(); failure = error; lock.unlock()
        done.signal()
    }
    func wait() throws {
        done.wait()
        lock.lock(); let error = failure; lock.unlock()
        if let error { throw error }
    }
}

func downloadArchive(from url: URL, to destination: URL, control: InstallControl) throws {
    try control.checkCancellation()
    guard url.scheme?.lowercased() == "https" else {
        throw NSError(domain: "SattariHub", code: 19,
                      userInfo: [NSLocalizedDescriptionKey: "Online updates require an HTTPS archive URL."])
    }
    let configuration = URLSessionConfiguration.ephemeral
    configuration.timeoutIntervalForRequest = 30
    configuration.timeoutIntervalForResource = 300
    let session = URLSession(configuration: configuration)
    defer { control.onCancel(nil); session.invalidateAndCancel() }
    let completion = DownloadCompletion()
    let task = session.downloadTask(with: url) { location, response, error in
        do {
            if let error { throw error }
            try control.checkCancellation()
            guard let location, let http = response as? HTTPURLResponse,
                  http.statusCode == 200, http.url?.scheme?.lowercased() == "https" else {
                throw NSError(domain: "SattariHub", code: 20,
                              userInfo: [NSLocalizedDescriptionKey: "The update server did not return a valid HTTPS archive."])
            }
            try FileManager.default.moveItem(at: location, to: destination)
            completion.finish(nil)
        } catch { completion.finish(error) }
    }
    control.onCancel { task.cancel() }
    task.resume()
    do { try completion.wait() }
    catch {
        try control.checkCancellation()
        throw error
    }
    try control.checkCancellation()
}

import AppKit

let canvasSize = NSSize(width: 1200, height: 630)
let canvas = NSImage(size: canvasSize)

func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, _ alpha: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: red / 255, green: green / 255, blue: blue / 255, alpha: alpha)
}

func image(named path: String) -> NSImage {
    guard let result = NSImage(contentsOfFile: path) else {
        fatalError("Missing image: \(path)")
    }
    return result
}

func drawAspectFit(_ source: NSImage, in target: NSRect) {
    let scale = min(target.width / source.size.width, target.height / source.size.height)
    let size = NSSize(width: source.size.width * scale, height: source.size.height * scale)
    let rect = NSRect(
        x: target.midX - size.width / 2,
        y: target.midY - size.height / 2,
        width: size.width,
        height: size.height
    )
    source.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1)
}

func drawProduct(_ source: NSImage, in rect: NSRect, radius: CGFloat) {
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.72)
    shadow.shadowBlurRadius = 26
    shadow.shadowOffset = NSSize(width: 0, height: -12)
    shadow.set()
    color(7, 9, 11).setFill()
    NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius).fill()
    NSGraphicsContext.restoreGraphicsState()

    NSGraphicsContext.saveGraphicsState()
    NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius).addClip()
    source.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1)
    NSGraphicsContext.restoreGraphicsState()

    color(91, 222, 175, 0.2).setStroke()
    let border = NSBezierPath(roundedRect: rect.insetBy(dx: 0.5, dy: 0.5), xRadius: radius, yRadius: radius)
    border.lineWidth = 1
    border.stroke()
}

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let logo = image(named: root.appendingPathComponent("assets/sattari-logo-white.png").path)
let mix = image(named: root.appendingPathComponent("assets/products/mix.png").path)
let key = image(named: root.appendingPathComponent("assets/products/song-key.png").path)
let scope = image(named: root.appendingPathComponent("assets/products/scope.png").path)

canvas.lockFocus()

let background = NSGradient(colors: [color(18, 23, 25), color(5, 7, 9)])!
background.draw(in: NSRect(origin: .zero, size: canvasSize), angle: -90)

color(70, 223, 169, 0.045).setStroke()
for x in stride(from: CGFloat(0), through: canvasSize.width, by: 40) {
    let line = NSBezierPath()
    line.move(to: NSPoint(x: x, y: 0))
    line.line(to: NSPoint(x: x, y: canvasSize.height))
    line.lineWidth = 1
    line.stroke()
}
for y in stride(from: CGFloat(0), through: canvasSize.height, by: 40) {
    let line = NSBezierPath()
    line.move(to: NSPoint(x: 0, y: y))
    line.line(to: NSPoint(x: canvasSize.width, y: y))
    line.lineWidth = 1
    line.stroke()
}

drawAspectFit(logo, in: NSRect(x: 438, y: 548, width: 324, height: 70))

let headline = "Four racks. One session key."
let paragraphStyle = NSMutableParagraphStyle()
paragraphStyle.alignment = .center
let headlineAttributes: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 52, weight: .semibold),
    .foregroundColor: color(248, 248, 250),
    .paragraphStyle: paragraphStyle,
    .kern: -1.4
]
headline.draw(
    in: NSRect(x: 80, y: 474, width: 1040, height: 66),
    withAttributes: headlineAttributes
)

drawProduct(mix, in: NSRect(x: 225, y: 25, width: 750, height: 458), radius: 15)
drawProduct(key, in: NSRect(x: 35, y: 12, width: 378, height: 232), radius: 14)
drawProduct(scope, in: NSRect(x: 787, y: 4, width: 378, height: 232), radius: 14)

canvas.unlockFocus()

guard
    let tiff = canvas.tiffRepresentation,
    let bitmap = NSBitmapImageRep(data: tiff),
    let jpeg = bitmap.representation(using: .jpeg, properties: [.compressionFactor: 0.9])
else {
    fatalError("Could not encode social card")
}

try jpeg.write(to: root.appendingPathComponent("assets/og.jpg"))

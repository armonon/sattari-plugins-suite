// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SattariHub",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "SattariHub", targets: ["SattariHub"])
    ],
    targets: [
        .executableTarget(
            name: "SattariHub",
            resources: [
                .copy("Resources/sattari-products.json")
            ]
        )
    ]
)

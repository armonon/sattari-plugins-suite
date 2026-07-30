import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  metadataBase: new URL("https://sattari-audio-studio.armonon.chatgpt.site"),
  title: "Sattari Audio — Four racks. One session key.",
  description:
    "A Mac plugin suite with four modular racks, 28 shared processors, and one live session key.",
  openGraph: {
    title: "Sattari Audio — Four racks. One session key.",
    description:
      "Four modular racks, 28 shared processors, and one live musical authority.",
    images: [{ url: "/assets/og.jpg", width: 1200, height: 630 }],
  },
  twitter: {
    card: "summary_large_image",
    title: "Sattari Audio — Four racks. One session key.",
    description:
      "Four modular racks, 28 shared processors, and one live musical authority.",
    images: ["/assets/og.jpg"],
  },
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

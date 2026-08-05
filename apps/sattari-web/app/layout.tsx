import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  metadataBase: new URL("https://sattari-audio-studio.armonon.chatgpt.site"),
  title: "Sattari Audio — Six products. One musical system.",
  description:
    "Forty-eight Mac plugins organized into six products, all connected by one live musical authority.",
  openGraph: {
    title: "Sattari Audio — Six products. One musical system.",
    description:
      "Compass, Auto Pitch, Instruments, Vocal, Mix and Create — one connected music-production suite.",
    images: [{ url: "/assets/og.png", width: 1200, height: 630 }],
  },
  twitter: {
    card: "summary_large_image",
    title: "Sattari Audio — Six products. One musical system.",
    description:
      "Forty-eight plugins. Six products. One live musical authority.",
    images: ["/assets/og.png"],
  },
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

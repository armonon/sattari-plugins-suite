import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  metadataBase: new URL("https://sattari-audio-studio.armonon.chatgpt.site"),
  title: "Sattari Audio — Six products. One musical system.",
  description:
    "Six focused Mac music products built from 48 connected components and one live musical authority.",
  openGraph: {
    title: "Sattari Audio — Six products. One musical system.",
    description:
      "Compass, Auto Pitch, Instruments, Vocal, Mix and Create — one connected music-production suite.",
    images: [{ url: "/assets/og-system-v2.png", width: 1200, height: 630 }],
  },
  twitter: {
    card: "summary_large_image",
    title: "Sattari Audio — Six products. One musical system.",
    description:
      "Six products. Forty-eight connected components. One live musical authority.",
    images: ["/assets/og-system-v2.png"],
  },
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

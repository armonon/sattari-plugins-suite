import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  metadataBase: new URL("https://sattari-audio-studio.armonon.chatgpt.site"),
  title: "Sattari Audio — Six connected music products for Mac",
  description:
    "Compass, Auto Pitch, Instruments, Vocal, Mix and Create — six connected Mac music products with one shared musical context.",
  openGraph: {
    title: "Sattari Audio — Six connected music products for Mac",
    description:
      "From session context to final mix: six focused products, one musical system.",
    images: [{ url: "/assets/og-six-alpha.png", width: 1200, height: 630 }],
  },
  twitter: {
    card: "summary_large_image",
    title: "Sattari Audio — Six connected music products for Mac",
    description:
      "From session context to final mix: six focused products, one musical system.",
    images: ["/assets/og-six-alpha.png"],
  },
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

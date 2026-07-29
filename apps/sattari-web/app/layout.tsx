import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  title: "Sattari Audio — Four racks. One session key.",
  description:
    "A Mac plugin suite with four modular racks, 28 shared processors, and one live session key.",
};

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}

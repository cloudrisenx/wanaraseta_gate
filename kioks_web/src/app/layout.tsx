import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Smart Gate Kiosk",
  description: "Wanara Seta Universal Kiosk Display",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className="h-full antialiased dark">
      <body className="min-h-full flex flex-col bg-black text-slate-100">{children}</body>
    </html>
  );
}

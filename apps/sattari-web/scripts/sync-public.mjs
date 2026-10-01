import { cp, mkdir } from "node:fs/promises";
import { resolve } from "node:path";

const root = process.cwd();
const publicDir = resolve(root, "public");
const files = [
  "_logosheet.html",
  "index.html",
  "logos.js",
  "plugin.html",
  "modular.html",
  "plugins-data.js",
  "product-catalog.js",
  "product-shots.js",
];

await mkdir(publicDir, { recursive: true });

for (const file of files) {
  await cp(resolve(root, file), resolve(publicDir, file), { force: true });
}

await cp(resolve(root, "assets"), resolve(publicDir, "assets"), {
  recursive: true,
  force: true,
});

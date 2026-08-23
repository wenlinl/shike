import fs from "fs";
import path from "path";
import crypto from "crypto";

const dataDir = process.env.DATA_DIR || path.join(process.cwd(), "data", "uploads");

export function ensureDir(dir: string) {
  fs.mkdirSync(dir, { recursive: true });
}

export function uploadsRoot() {
  ensureDir(dataDir);
  return dataDir;
}

export function saveUpload(
  kind: "ppt" | "audio" | "xzd",
  filename: string,
  buffer: Buffer,
): string {
  const root = uploadsRoot();
  const dir = path.join(root, kind);
  ensureDir(dir);
  const ext = path.extname(filename).toLowerCase();
  const name = `${crypto.randomUUID()}${ext || ".bin"}`;
  const filePath = path.join(dir, name);
  fs.writeFileSync(filePath, buffer);
  return filePath;
}

export function deleteFile(filePath?: string | null) {
  if (!filePath) return;
  try {
    fs.unlinkSync(filePath);
  } catch {
    // ignore
  }
}

export function fileSizeMB(filePath: string): number {
  try {
    return fs.statSync(filePath).size / (1024 * 1024);
  } catch {
    return 0;
  }
}

export function readFile(filePath: string): Buffer {
  return fs.readFileSync(filePath);
}

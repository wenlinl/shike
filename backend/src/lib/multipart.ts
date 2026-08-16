import { NextRequest } from "next/server";
import busboy from "busboy";
import { Readable } from "stream";

export type UploadedFile = {
  field: string;
  filename: string;
  mimeType: string;
  buffer: Buffer;
};

export async function parseMultipart(
  req: NextRequest,
  opts: { maxFileSizeMB?: number } = {},
) {
  const maxBytes = (opts.maxFileSizeMB ?? 100) * 1024 * 1024;
  const contentType = req.headers.get("content-type") || "";
  const bb = busboy({ headers: { "content-type": contentType } });

  const fields: Record<string, string> = {};
  const files: UploadedFile[] = [];

  await new Promise<void>((resolve, reject) => {
    bb.on("field", (name, val) => {
      fields[name] = val;
    });
    bb.on("file", (name, stream, info) => {
      const chunks: Buffer[] = [];
      let size = 0;
      let tooBig = false;
      stream.on("data", (c: Buffer) => {
        size += c.length;
        if (size > maxBytes) {
          tooBig = true;
          stream.destroy(new Error("文件超过大小限制"));
          return;
        }
        chunks.push(c);
      });
      stream.on("error", (err) => {
        if (tooBig) reject(new Error("文件超过大小限制"));
        else reject(err);
      });
      stream.on("end", () => {
        if (!tooBig) {
          files.push({
            field: name,
            filename: info.filename,
            mimeType: info.mimeType,
            buffer: Buffer.concat(chunks),
          });
        }
      });
    });
    bb.on("error", reject);
    bb.on("close", resolve);

    const body = req.body;
    if (body) {
      Readable.fromWeb(body as import("stream/web").ReadableStream).pipe(bb);
    } else {
      reject(new Error("请求体为空"));
    }
  });

  return { fields, files };
}

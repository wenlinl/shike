import "server-only";
import { parseJsonLoose } from "@/lib/ai";

export type XzdScanItem = {
  name: string;
  category: string;
  expiryDate: string | null;
  suggestedContainer: string;
  confidence: number;
  note: string;
};

const SYSTEM_PROMPT = `你是“食刻”食品识别助手。根据照片识别食品，只输出 JSON：
{"name":"不超过10字中文品名","category":"乳制品|肉类|蔬菜|水果|饮料|零食|调味品|药品|主食|其他","expiryDate":"YYYY-MM-DD 或空","suggestedContainer":"冰箱|零食柜|药盒|调料柜|主食柜","confidence":0.0~1.0,"note":"看不清时的一句话"}
规则：优先读包装日期；读不到按常识估计；看不清不要猜名字，confidence 给低分（<0.5）。`;

export async function parseFoodPhoto(imageBase64: string): Promise<XzdScanItem> {
  const base = process.env.ARK_BASE_URL || "https://ark.cn-beijing.volces.com/api/v3";
  const apiKey = process.env.ARK_VISION_API_KEY || process.env.ARK_API_KEY;
  if (!apiKey) {
    throw new Error("未配置 ARK_VISION_API_KEY / ARK_API_KEY");
  }
  const model =
    process.env.ARK_VISION_MODEL ||
    process.env.ARK_CHAT_MODEL ||
    "doubao-seed-2-1-turbo-260628";

  const res = await fetch(`${base}/chat/completions`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${apiKey}`,
    },
    body: JSON.stringify({
      model,
      messages: [
        { role: "system", content: SYSTEM_PROMPT },
        {
          role: "user",
          content: [
            { type: "text", text: "请识别这张照片中的食品。" },
            { type: "image_url", image_url: { url: `data:image/jpeg;base64,${imageBase64}` } },
          ],
        },
      ],
      temperature: 0.1,
      max_tokens: 250,
      thinking: { type: "disabled" },
    }),
    signal: AbortSignal.timeout(60_000),
  });

  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`AI 接口调用失败 (${res.status}): ${text.slice(0, 500)}`);
  }

  const data = await res.json();
  const content: string | undefined = data?.choices?.[0]?.message?.content;
  if (!content) throw new Error("AI 接口未返回内容");

  const raw = parseJsonLoose<Partial<XzdScanItem>>(content);
  return {
    name: String(raw.name ?? "").trim().slice(0, 20),
    category: String(raw.category ?? "其他").trim(),
    expiryDate: String(raw.expiryDate ?? "").trim() || null,
    suggestedContainer: String(raw.suggestedContainer ?? "").trim(),
    confidence: Number(raw.confidence ?? 0),
    note: String(raw.note ?? "").trim(),
  };
}

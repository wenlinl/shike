import "server-only";

export async function chatJson<T>(
  system: string,
  user: string,
  opts: { temperature?: number; maxTokens?: number } = {},
): Promise<T> {
  const base = process.env.ARK_BASE_URL || "https://ark.cn-beijing.volces.com/api/v3";
  const apiKey = process.env.ARK_API_KEY;
  if (!apiKey) {
    throw new Error("未配置 ARK_API_KEY，请在 .env 中填写火山方舟 API Key");
  }
  const model =
    process.env.ARK_CHAT_MODEL || "doubao-1-5-pro-32k-250115";

  const res = await fetch(`${base}/chat/completions`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${apiKey}`,
    },
    body: JSON.stringify({
      model,
      messages: [
        { role: "system", content: system },
        { role: "user", content: user },
      ],
      temperature: opts.temperature ?? 0.3,
      max_tokens: opts.maxTokens ?? 4000,
      response_format: { type: "json_object" },
    }),
    signal: AbortSignal.timeout(180_000),
  });

  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`AI 接口调用失败 (${res.status}): ${text.slice(0, 500)}`);
  }

  const data = await res.json();
  const content: string | undefined = data?.choices?.[0]?.message?.content;
  if (!content) {
    throw new Error("AI 接口未返回内容");
  }

  return parseJsonLoose<T>(content);
}

export function parseJsonLoose<T>(content: string): T {
  try {
    return JSON.parse(content) as T;
  } catch {
    // 尝试提取第一个 {...} 或 [...] 块
    const m = content.match(/(\{[\s\S]*\}|\[[\s\S]*\])/);
    if (m) {
      try {
        return JSON.parse(m[1]) as T;
      } catch {
        // fallthrough
      }
    }
    throw new Error(`无法解析 AI 返回内容: ${content.slice(0, 300)}`);
  }
}

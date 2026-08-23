import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

async function resolveItemId(name?: string | null, itemId?: string | null) {
  if (itemId) return itemId;
  if (!name) return null;
  const it = await prisma.foodItem.findFirst({ where: { name: { contains: name } } });
  return it?.id || null;
}

export async function GET(req: NextRequest) {
  const name = req.nextUrl.searchParams.get("name") || undefined;
  const itemId = req.nextUrl.searchParams.get("itemId") || undefined;
  const id = await resolveItemId(name, itemId);
  const trace = id ? await prisma.foodTrace.findUnique({ where: { itemId: id } }) : null;
  return xzdJson({
    code: 0,
    trace: trace
      ? {
          itemId: trace.itemId,
          source: trace.source,
          origin: trace.origin,
          batch: trace.batch,
          coldChain: trace.coldChain,
          report: trace.report,
          inTime: trace.inTime,
        }
      : null,
  });
}

export async function POST(req: NextRequest) {
  const body = (await req.json().catch(() => null)) as {
    name?: string;
    itemId?: string;
    source?: string;
    origin?: string;
    batch?: string;
    coldChain?: string;
    report?: string;
    inTime?: string;
  } | null;
  const id = await resolveItemId(body?.name, body?.itemId);
  if (!id) return xzdJson({ code: 1, msg: "未找到对应库存食材" });
  const trace = await prisma.foodTrace.upsert({
    where: { itemId: id },
    update: {
      source: body?.source ?? undefined,
      origin: body?.origin ?? undefined,
      batch: body?.batch ?? undefined,
      coldChain: body?.coldChain ?? undefined,
      report: body?.report ?? undefined,
      inTime: body?.inTime ? new Date(body.inTime) : undefined,
    },
    create: {
      itemId: id,
      source: body?.source ?? null,
      origin: body?.origin ?? null,
      batch: body?.batch ?? null,
      coldChain: body?.coldChain ?? null,
      report: body?.report ?? null,
      inTime: body?.inTime ? new Date(body.inTime) : null,
    },
  });
  return xzdJson({ code: 0, trace });
}

export { xzdOptions as OPTIONS };

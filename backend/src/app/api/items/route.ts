import { NextRequest } from "next/server";
import { prisma } from "@/lib/db";
import { xzdJson, xzdOptions } from "@/lib/http";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const CONTAINERS = ["冰箱", "零食柜", "药盒", "调料柜", "主食柜"];

function startOfDay(d: Date) {
  return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()));
}

/** 库存列表：按容器筛选，按剩余天数升序（软件端大屏 / H5 / 设备共用）。 */
export async function GET(req: NextRequest) {
  const container = req.nextUrl.searchParams.get("container");
  const where =
    container && container !== "全部" && container !== "" ? { container } : {};
  const [rows, members] = await Promise.all([
    prisma.foodItem.findMany({
      where,
      orderBy: [{ daysLeft: "asc" }, { scannedAt: "desc" }],
    }),
    prisma.member.findMany({ select: { id: true, name: true, avatar: true } }),
  ]);
  const memberMap = new Map(members.map((m) => [m.id, m]));
  const today = startOfDay(new Date());
  const items = rows.map((it) => {
    const daysLeft = it.expiryDate
      ? Math.round((startOfDay(it.expiryDate).getTime() - today.getTime()) / 86400000)
      : it.daysLeft;
    const member = it.memberId ? memberMap.get(it.memberId) : null;
    return {
      id: it.id,
      name: it.name,
      category: it.category,
      emoji: it.emoji,
      container: it.container,
      quantity: it.quantity,
      recordMethod: it.recordMethod,
      keepDays: it.keepDays,
      storage: it.storage,
      label: it.label,
      recordedBy: member?.name || null,
      recordedByAvatar: member?.avatar || null,
      expiryDate: it.expiryDate ? it.expiryDate.toISOString().slice(0, 10) : null,
      scannedAt: it.scannedAt.toISOString(),
      daysLeft,
      confidence: it.confidence,
      note: it.note,
      status: daysLeft === -1 ? "unknown" : daysLeft < 0 ? "expired" : daysLeft <= 3 ? "soon" : "normal",
    };
  });
  return xzdJson({ code: 0, items });
}

/** 手动录入（H5 快速录入）：无图片，直接写库存。 */
export async function POST(req: NextRequest) {
  const body = (await req.json().catch(() => null)) as {
    name?: string;
    category?: string;
    container?: string;
    expiryDays?: number;
    expiryDate?: string;
    keepDays?: number;
    quantity?: number;
    memberId?: string;
    deviceId?: string;
    note?: string;
  } | null;
  const name = (body?.name || "").trim().slice(0, 40);
  if (!name) return xzdJson({ code: 1, msg: "name 必填" });
  const container = CONTAINERS.includes(body?.container ?? "") ? body!.container! : "冰箱";
  const deviceId = (body?.deviceId || "h5-demo-001").slice(0, 64);
  const scannedAt = new Date();
  const expiryDate = body?.expiryDate
    ? new Date(body.expiryDate)
    : new Date(scannedAt.getTime() + (Math.max(1, Number(body?.expiryDays) || 3)) * 86400000);
  const daysLeft = Math.round(
    (startOfDay(expiryDate).getTime() - startOfDay(scannedAt).getTime()) / 86400000,
  );
  const quantity = Math.max(1, Math.min(999, Number(body?.quantity) || 1));
  const keepDays = body?.keepDays != null ? Math.max(1, Number(body.keepDays)) : null;

  const key = { name, container, deviceId };
  const exist = await prisma.foodItem.findUnique({ where: { name_container_deviceId: key } });
  if (exist) {
    await prisma.foodItem.update({
      where: { id: exist.id },
      data: {
        quantity: exist.quantity + quantity,
        category: body?.category || undefined,
        expiryDate,
        daysLeft,
        keepDays,
        recordMethod: "manual",
        memberId: body?.memberId || undefined,
        scannedAt,
      },
    });
  } else {
    await prisma.foodItem.create({
      data: {
        deviceId,
        memberId: body?.memberId || null,
        name,
        category: body?.category || null,
        container,
        quantity,
        recordMethod: "manual",
        keepDays,
        scannedAt,
        expiryDate,
        daysLeft,
        confidence: 1,
        note: body?.note || null,
      },
    });
  }
  await prisma.scanLog.create({
    data: {
      deviceId,
      memberId: body?.memberId || null,
      action: "in",
      name,
      category: body?.category || null,
      container,
      recordMethod: "manual",
      keepDays,
      scannedAt,
      expiryDate,
      daysLeft,
      confidence: 1,
    },
  });
  return xzdJson({
    code: 0,
    name,
    container,
    expiryDate: expiryDate.toISOString().slice(0, 10),
    daysLeft,
  });
}

export { xzdOptions as OPTIONS };

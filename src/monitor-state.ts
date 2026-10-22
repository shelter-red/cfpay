import { DurableObject } from "cloudflare:workers";
import type { NormalizedComponentStatus, WechatChannelStatus } from "./monitor";
import type { Env } from "./types";
import type { NormalizedWechatLoginState, WechatLoginSessionView } from "./wechat-login";

export const MONITOR_STATE_NAME = "single-merchant-monitor-state";
/** 监控端每 5 秒统一上报一次，连续三次未收到即判定设备和组件失联。 */
export const MONITOR_ONLINE_WINDOW_MS = 15_000;
const NONCE_TTL_MS = 10 * 60_000;

interface MonitorDeviceView {
  id: string;
  name: string | null;
  version: string | null;
  platform: string | null;
  last_ip: string | null;
  last_heartbeat_at: number;
}

interface MonitorComponentView {
  device_id: string;
  component: string;
  status: string;
  component_version: string | null;
  target_version: string | null;
  process_id: number | null;
  hook_offset: string | null;
  detail_code: string | null;
  detail_message: string | null;
  last_event_at: number | null;
  reported_at: number;
  updated_at: number;
}

export interface MonitorSnapshot {
  devices: MonitorDeviceView[];
  components: MonitorComponentView[];
  wechatLogin: Array<WechatLoginSessionView & { qrUrl: string | null }>;
  wechatChannel: WechatChannelStatus;
  onlineWindowMs: number;
  serverTime: number;
}

export interface MonitorLiveUpdate {
  device: MonitorDeviceView;
  component?: MonitorComponentView;
  wechatLogin?: WechatLoginSessionView & { qrUrl: string | null };
  serverTime: number;
}

export interface MonitorStatusUpdate {
  deviceId: string;
  nonce: string;
  name: string | null;
  version: string | null;
  platform: string | null;
  ip: string | null;
  receivedAt: number;
  component: NormalizedComponentStatus | null;
  login: NormalizedWechatLoginState | null;
}

interface StoredLogin extends WechatLoginSessionView {
  qrPayload: string | null;
}

function stateStub(env: Env): DurableObjectStub {
  return env.MONITOR_STATE.get(env.MONITOR_STATE.idFromName(MONITOR_STATE_NAME));
}

async function requireOk(response: Response): Promise<Response> {
  if (response.ok) return response;
  const data: { error?: string; message?: string } = await response
    .json<{ error?: string; message?: string }>()
    .catch(() => ({}));
  throw new Error(data.error || data.message || `MONITOR_STATE_HTTP_${response.status}`);
}

export async function recordMonitorStatus(env: Env, update: MonitorStatusUpdate): Promise<MonitorSnapshot> {
  const response = await stateStub(env).fetch("https://monitor-state/status", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(update),
  });
  return (await requireOk(response)).json<MonitorSnapshot>();
}

export async function claimMonitorNonce(env: Env, deviceId: string, nonce: string, now = Date.now()): Promise<void> {
  const response = await stateStub(env).fetch("https://monitor-state/nonce", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ deviceId, nonce, expiresAt: now + NONCE_TTL_MS }),
  });
  await requireOk(response);
}

export async function getMonitorSnapshot(env: Env): Promise<MonitorSnapshot> {
  const response = await stateStub(env).fetch("https://monitor-state/snapshot");
  return (await requireOk(response)).json<MonitorSnapshot>();
}

export async function getMonitorWechatChannelStatus(env: Env): Promise<WechatChannelStatus> {
  const response = await stateStub(env).fetch("https://monitor-state/channel/wechat");
  return (await requireOk(response)).json<WechatChannelStatus>();
}

export async function getMonitorLoginQrPayload(env: Env, deviceId: string): Promise<string | null> {
  const url = new URL("https://monitor-state/qr");
  url.searchParams.set("deviceId", deviceId);
  const response = await stateStub(env).fetch(url);
  if (response.status === 404) return null;
  const data = await requireOk(response).then((result) => result.json<{ payload: string }>());
  return data.payload;
}

export function getMonitorLiveResponse(env: Env, request: Request): Promise<Response> {
  return stateStub(env).fetch(new Request("https://monitor-state/live", request));
}

export class MonitorState extends DurableObject<Env> {
  private readonly devices = new Map<string, MonitorDeviceView>();
  private readonly components = new Map<string, MonitorComponentView>();
  private readonly login = new Map<string, StoredLogin>();
  private readonly nonces = new Map<string, number>();

  constructor(state: DurableObjectState, env: Env) {
    super(state, env);
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const now = Date.now();
    this.prune(now);

    if (request.method === "POST" && url.pathname === "/status") {
      const update = await request.json<MonitorStatusUpdate>();
      if (!this.claimNonce(update.deviceId, update.nonce, update.receivedAt + NONCE_TTL_MS, now)) {
        return Response.json({ error: "MONITOR_REPLAY_DETECTED", message: "监控端请求已重复提交" }, { status: 409 });
      }
      this.applyStatus(update);
      const snapshot = this.snapshot(now);
      this.broadcastUpdate(this.liveUpdate(update, now));
      return Response.json(snapshot);
    }

    if (request.method === "POST" && url.pathname === "/nonce") {
      const payload = await request.json<{ deviceId: string; nonce: string; expiresAt: number }>();
      if (!this.claimNonce(payload.deviceId, payload.nonce, payload.expiresAt, now)) {
        return Response.json({ error: "MONITOR_REPLAY_DETECTED", message: "监控端请求已重复提交" }, { status: 409 });
      }
      return Response.json({ ok: true, serverTime: now });
    }

    if (request.method === "GET" && url.pathname === "/snapshot") return Response.json(this.snapshot(now));
    if (request.method === "GET" && url.pathname === "/channel/wechat") return Response.json(this.wechatStatus(now));

    if (request.method === "GET" && url.pathname === "/qr") {
      const deviceId = url.searchParams.get("deviceId") ?? "";
      const login = this.login.get(deviceId);
      if (!login || login.status !== "QR_READY" || !login.qrPayload || login.expiresAt <= now) {
        return new Response("Not Found", { status: 404 });
      }
      return Response.json({ payload: login.qrPayload });
    }

    if (request.method === "GET" && url.pathname === "/live" && request.headers.get("upgrade")?.toLowerCase() === "websocket") {
      const pair = new WebSocketPair();
      const client = pair[0];
      const server = pair[1];
      this.ctx.acceptWebSocket(server);
      server.send(JSON.stringify({ type: "snapshot", data: this.snapshot(now) }));
      return new Response(null, { status: 101, webSocket: client });
    }

    return new Response("Not Found", { status: 404 });
  }

  private claimNonce(deviceId: string, nonce: string, expiresAt: number, now: number): boolean {
    const key = `${deviceId}:${nonce}`;
    if (this.nonces.has(key)) return false;
    this.nonces.set(key, Math.max(expiresAt, now + 1));
    return true;
  }

  private applyStatus(update: MonitorStatusUpdate): void {
    this.devices.set(update.deviceId, {
      id: update.deviceId,
      name: update.name,
      version: update.version,
      platform: update.platform,
      last_ip: update.ip,
      last_heartbeat_at: update.receivedAt,
    });

    if (update.component) {
      const component = update.component;
      this.components.set(`${update.deviceId}:${component.component}`, {
        device_id: update.deviceId,
        component: component.component,
        status: component.status,
        component_version: component.componentVersion,
        target_version: component.targetVersion,
        process_id: component.processId,
        hook_offset: component.hookOffset,
        detail_code: component.detailCode,
        detail_message: component.detailMessage,
        last_event_at: component.lastEventAt,
        reported_at: update.receivedAt,
        updated_at: update.receivedAt,
      });
    }

    if (update.login) {
      this.login.set(update.deviceId, {
        deviceId: update.deviceId,
        status: update.login.status,
        observedAt: update.login.observedAt,
        expiresAt: update.login.expiresAt,
        updatedAt: update.receivedAt,
        qrAvailable: Boolean(update.login.qrPayload),
        qrPayload: update.login.qrPayload,
      });
    }
  }

  private snapshot(now: number): MonitorSnapshot {
    const live = new Set(
      [...this.devices.values()]
      .filter((device) => device.last_heartbeat_at > now - MONITOR_ONLINE_WINDOW_MS)
        .map((device) => device.id),
    );
    return {
      devices: [...this.devices.values()].filter((device) => live.has(device.id)).sort((a, b) => b.last_heartbeat_at - a.last_heartbeat_at),
      components: [...this.components.values()].filter((component) => live.has(component.device_id)),
      wechatLogin: [...this.login.values()]
        .filter((session) => live.has(session.deviceId) && session.expiresAt > now)
        .map(({ qrPayload: _payload, ...session }) => ({
          ...session,
          qrUrl: session.qrAvailable
            ? `/admin-api/wechat-login/${encodeURIComponent(session.deviceId)}/qr?v=${session.updatedAt}`
            : null,
        })),
      wechatChannel: this.wechatStatus(now),
      onlineWindowMs: MONITOR_ONLINE_WINDOW_MS,
      serverTime: now,
    };
  }

  // 生成单次状态上报对应的增量消息；首次连接仍发送完整快照，后续心跳只推当前设备变化。
  private liveUpdate(update: MonitorStatusUpdate, now: number): MonitorLiveUpdate {
    const device = this.devices.get(update.deviceId);
    if (!device) throw new Error("MONITOR_DEVICE_NOT_FOUND");
    const component = update.component ? this.components.get(`${update.deviceId}:${update.component.component}`) : undefined;
    const login = update.login ? this.login.get(update.deviceId) : undefined;
    return {
      device,
      ...(component ? { component } : {}),
      ...(login
        ? {
            wechatLogin: {
              deviceId: login.deviceId,
              status: login.status,
              observedAt: login.observedAt,
              expiresAt: login.expiresAt,
              updatedAt: login.updatedAt,
              qrAvailable: login.qrAvailable,
              qrUrl: login.qrAvailable
                ? `/admin-api/wechat-login/${encodeURIComponent(login.deviceId)}/qr?v=${login.updatedAt}`
                : null,
            },
          }
        : {}),
      serverTime: now,
    };
  }

  private wechatStatus(now: number): WechatChannelStatus {
    const component = [...this.components.values()]
      .filter((item) => item.component === "wechat_hook")
      .sort((a, b) => b.reported_at - a.reported_at)[0];
    if (!component) {
      return { available: false, reason: "NO_MONITOR", componentStatus: null, detailCode: null, deviceId: null, reportedAt: null, loginStatus: null };
    }
    const device = this.devices.get(component.device_id);
    const login = this.login.get(component.device_id);
    const observed = {
      componentStatus: component.status,
      detailCode: component.detail_code,
      deviceId: component.device_id,
      reportedAt: component.reported_at,
      loginStatus: login?.status ?? null,
    };
    if (!device || device.last_heartbeat_at <= now - MONITOR_ONLINE_WINDOW_MS || component.reported_at <= now - MONITOR_ONLINE_WINDOW_MS) {
      return { available: false, reason: "STALE", ...observed };
    }
    if (component.status !== "HEALTHY" || login?.status !== "LOGGED_IN") {
      return { available: false, reason: "UNHEALTHY", ...observed };
    }
    return { available: true, reason: "OK", ...observed };
  }

  private prune(now: number): void {
    for (const [key, expiresAt] of this.nonces) if (expiresAt <= now) this.nonces.delete(key);
    for (const [deviceId, session] of this.login) if (session.expiresAt <= now) this.login.delete(deviceId);
    for (const [deviceId, device] of this.devices) {
      if (device.last_heartbeat_at >= now - 10 * 60_000) continue;
      this.devices.delete(deviceId);
      this.login.delete(deviceId);
      for (const [key, component] of this.components) if (component.device_id === deviceId) this.components.delete(key);
    }
  }

  // 向已连接管理端广播单设备增量，避免每 5 秒重复发送全部设备、组件和派生通道状态。
  private broadcastUpdate(update: MonitorLiveUpdate): void {
    const message = JSON.stringify({ type: "update", data: update });
    for (const socket of this.ctx.getWebSockets()) {
      try {
        socket.send(message);
      } catch {
        try { socket.close(1011, "状态推送失败"); } catch { /* 连接已经关闭 */ }
      }
    }
  }
}

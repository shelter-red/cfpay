import { onUnmounted, ref } from "vue";
import { toast } from "vue-sonner";
import { api, errorMessage } from "@/lib/api";
import type {
  MonitorComponent, MonitorDevice, MonitorLiveUpdate, MonitorsResponse, WechatLoginSession,
} from "@/lib/types";

const FALLBACK_POLL_INTERVAL_MS = 2_000;
const RECONNECT_INTERVAL_MS = 3_000;
// 唯一的新鲜度窗口：监控端每 5 秒上报一次，连续 3 次未收到即失联。
const ONLINE_WINDOW_MS = 15_000;

/**
 * 监控端数据的统一来源：设备、组件状态与微信登录会话来自同一个接口。
 * 轮询、实时连接与二维码弹窗状态都集中在这里。
 */
export function useMonitors(options: { poll?: boolean } = {}) {
  const poll = options.poll !== false;
  const devices = ref<MonitorDevice[]>([]);
  const components = ref<MonitorComponent[]>([]);
  const sessions = ref<WechatLoginSession[]>([]);
  const onlineWindowMs = ref(ONLINE_WINDOW_MS);
  const loaded = ref(false);
  const refreshing = ref(false);
  const failure = ref("");
  const nowMs = ref(Date.now());
  const realtimeConnected = ref(false);

  const qrDialogOpen = ref(false);
  const qrSession = ref<WechatLoginSession | null>(null);

  let pollTimer: ReturnType<typeof setTimeout> | null = null;
  let clockTimer: ReturnType<typeof setInterval> | null = null;
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  let socket: WebSocket | null = null;
  let serverOffsetMs = 0;

  function componentsOf(deviceId: string): MonitorComponent[] {
    return components.value.filter((component) => component.device_id === deviceId);
  }

  function sessionOf(deviceId: string): WechatLoginSession | undefined {
    return sessions.value.find((session) => session.deviceId === deviceId);
  }

  function isStale(component: MonitorComponent): boolean {
    return nowMs.value - Number(component.reported_at) >= onlineWindowMs.value;
  }

  /** 登录成功或二维码失效时自动关闭弹窗；收到新二维码时自动换图。 */
  function syncQrDialog(): void {
    const current = qrSession.value;
    if (!qrDialogOpen.value || !current) return;
    const session = sessions.value.find((item) => item.deviceId === current.deviceId) ?? null;
    if (
      !session ||
      session.status === "LOGGED_IN" ||
      !session.qrAvailable ||
      !session.qrUrl ||
      Number(session.expiresAt) <= nowMs.value
    ) {
      qrDialogOpen.value = false;
      qrSession.value = null;
      if (session?.status === "LOGGED_IN") toast.success("微信已登录，二维码弹窗已关闭");
      return;
    }
    const renewed = Number(session.updatedAt) !== Number(current.updatedAt);
    qrSession.value = session;
    if (renewed) toast.info("已收到新的登录二维码");
  }

  function openQr(session: WechatLoginSession): void {
    qrSession.value = session;
    qrDialogOpen.value = true;
  }

  async function reload(silent = false): Promise<void> {
    if (silent) refreshing.value = true;
    try {
      const data = await api<MonitorsResponse>("/admin-api/monitors");
      applySnapshot(data);
      failure.value = "";
    } catch (error) {
      if (!silent) failure.value = errorMessage(error);
    } finally {
      loaded.value = true;
      refreshing.value = false;
      schedule();
    }
  }

  function schedule(): void {
    if (!poll || realtimeConnected.value) return;
    if (pollTimer) clearTimeout(pollTimer);
    pollTimer = setTimeout(() => void reload(true), FALLBACK_POLL_INTERVAL_MS);
  }

  function applySnapshot(data: MonitorsResponse): void {
    if (Number.isFinite(Number(data.serverTime))) serverOffsetMs = Number(data.serverTime) - Date.now();
    nowMs.value = Date.now() + serverOffsetMs;
    devices.value = data.devices ?? [];
    components.value = data.components ?? [];
    sessions.value = data.wechatLogin ?? [];
    onlineWindowMs.value = data.onlineWindowMs ?? ONLINE_WINDOW_MS;
    syncQrDialog();
  }

  /** 合并 WebSocket 单设备增量，避免每次心跳覆盖整份设备快照。 */
  function applyUpdate(data: MonitorLiveUpdate): void {
    if (Number.isFinite(Number(data.serverTime))) serverOffsetMs = Number(data.serverTime) - Date.now();
    nowMs.value = Date.now() + serverOffsetMs;
    devices.value = [data.device, ...devices.value.filter((item) => item.id !== data.device.id)]
      .sort((left, right) => right.last_heartbeat_at - left.last_heartbeat_at);
    if (data.component) {
      components.value = [
        data.component,
        ...components.value.filter((item) => item.device_id !== data.component!.device_id || item.component !== data.component!.component),
      ];
    }
    if (data.wechatLogin) {
      sessions.value = [data.wechatLogin, ...sessions.value.filter((item) => item.deviceId !== data.wechatLogin!.deviceId)];
    }
    syncQrDialog();
  }

  function connectRealtime(): void {
    if (!poll || socket) return;
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    socket = new WebSocket(`${protocol}//${window.location.host}/admin-api/monitors/live`);
    socket.addEventListener("open", () => {
      realtimeConnected.value = true;
      if (pollTimer) clearTimeout(pollTimer);
      pollTimer = null;
    });
    socket.addEventListener("message", (event) => {
      try {
        const message = JSON.parse(String(event.data)) as { type?: string; data?: MonitorsResponse | MonitorLiveUpdate };
        if (message.type === "snapshot" && message.data) applySnapshot(message.data as MonitorsResponse);
        else if (message.type === "update" && message.data) applyUpdate(message.data as MonitorLiveUpdate);
      } catch {
        failure.value = "实时状态数据格式错误，已切换轮询";
        socket?.close();
      }
    });
    socket.addEventListener("close", () => {
      socket = null;
      realtimeConnected.value = false;
      schedule();
      if (reconnectTimer) clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(connectRealtime, RECONNECT_INTERVAL_MS);
    });
    socket.addEventListener("error", () => socket?.close());
  }

  function start(): void {
    clockTimer = setInterval(() => (nowMs.value = Date.now() + serverOffsetMs), 1000);
    void reload();
    connectRealtime();
  }

  function stop(): void {
    if (pollTimer) clearTimeout(pollTimer);
    if (clockTimer) clearInterval(clockTimer);
    if (reconnectTimer) clearTimeout(reconnectTimer);
    socket?.close();
    socket = null;
    pollTimer = null;
    clockTimer = null;
    reconnectTimer = null;
  }

  onUnmounted(stop);

  return {
    devices, components, sessions, onlineWindowMs, loaded, refreshing, failure, nowMs, realtimeConnected,
    qrDialogOpen, qrSession, componentsOf, sessionOf, isStale, openQr, reload, start, stop,
  };
}

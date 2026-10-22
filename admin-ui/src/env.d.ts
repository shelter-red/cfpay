/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** 可选的更新清单地址（远程 package.json）；未配置时不检查更新。 */
  readonly VITE_UPDATE_MANIFEST_URL?: string;
  /** 可选的项目仓库地址；未配置时隐藏侧栏入口。 */
  readonly VITE_REPOSITORY_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}

declare module "*.vue" {
  import type { DefineComponent } from "vue";

  const component: DefineComponent<Record<string, unknown>, Record<string, unknown>, unknown>;
  export default component;
}

interface Window {
  /** 由 Worker 在下发 index.html 时注入的真实后台入口，例如 "/cfpay"。 */
  __CFPAY_ADMIN_BASE__?: string;
}

/** 构建时从 Windows 监控端版本头注入，保证管理端显示同一个发布版本。 */
declare const __APP_VERSION__: string;

interface WechatMonitorDownloadInfo {
  filename: string;
  monitorVersion: string;
  weixinVersion: string;
  size: number;
  sha256: string;
}

/** 构建时由单 EXE 构建元数据注入，下载文件本身随管理端静态资源部署。 */
declare const __WECHAT_MONITOR_DOWNLOAD__: WechatMonitorDownloadInfo;

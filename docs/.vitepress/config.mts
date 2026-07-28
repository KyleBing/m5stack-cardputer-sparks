import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitepress'

// 与固件同源：只改 include/app_version.h，文档 nav / footer / md 占位符自动同步
const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const vpDir = dirname(fileURLToPath(import.meta.url))
const versionH = readFileSync(resolve(rootDir, 'include/app_version.h'), 'utf-8')
const pick = (key: string, fallback = '') =>
  versionH.match(new RegExp(`${key}\\s*=\\s*"([^"]+)"`))?.[1] ?? fallback

const APP_VERSION = pick('APP_VERSION', '0.0')
const APP_UPDATE_TIME = pick('APP_UPDATE_TIME')
const APP_AUTHOR = pick('APP_AUTHOR', 'KyleBing')
const DOC_VERSION = `v${APP_VERSION}`

// GitHub Pages 项目站 base；多版本时 DOCS_VERSION_PATH=v/1.02 → .../v/1.02/
const SITE_ROOT = process.env.GITHUB_ACTIONS ? '/m5stack-cardputer-sparks' : ''
const VERSION_PATH = (process.env.DOCS_VERSION_PATH || '').replace(/^\/|\/$/g, '')
const base = SITE_ROOT
  ? `${SITE_ROOT}/${VERSION_PATH ? `${VERSION_PATH}/` : ''}`
  : VERSION_PATH
    ? `/${VERSION_PATH}/`
    : '/'

// 多版本构建脚本写入；缺失时仅当前版本（本地 docs:dev）
type DocVersionEntry = {
  label: string
  version: string
  path: string
  latest?: boolean
}
function loadDocVersions(): DocVersionEntry[] {
  const p = resolve(vpDir, 'versions.generated.json')
  if (!existsSync(p)) {
    return [{ label: `${DOC_VERSION} (latest)`, version: DOC_VERSION, path: '', latest: true }]
  }
  try {
    return JSON.parse(readFileSync(p, 'utf-8')) as DocVersionEntry[]
  } catch {
    return [{ label: `${DOC_VERSION} (latest)`, version: DOC_VERSION, path: '', latest: true }]
  }
}
const DOC_VERSIONS = loadDocVersions()

// 归档构建写入：覆盖默认侧栏，使历史版导航与该版 md 一致
type SidebarGroup = { text?: string; link?: string; items?: SidebarGroup[]; collapsed?: boolean }
function loadArchiveSidebars(): { root?: SidebarGroup[]; en?: SidebarGroup[] | null } | null {
  const p = resolve(vpDir, 'sidebars.generated.json')
  if (!existsSync(p)) return null
  try {
    return JSON.parse(readFileSync(p, 'utf-8')) as { root?: SidebarGroup[]; en?: SidebarGroup[] | null }
  } catch {
    return null
  }
}
const ARCHIVE_SIDEBARS = loadArchiveSidebars()
const HAS_EN_DOCS = existsSync(resolve(vpDir, '../en/index.md'))

const sharedTheme = {
  logo: '/assets/logo_60.png',
  siteTitle: 'Sparks',
  socialLinks: [
    { icon: 'github', link: 'https://github.com/KyleBing/m5stack-cardputer-sparks' },
  ],
  search: {
    provider: 'local' as const,
  },
  footer: {
    message: 'Sparks for M5Stack Cardputer',
    copyright: `${DOC_VERSION} · ${APP_AUTHOR}`,
  },
  // VersionSwitcher 读取（用 currentPath 高亮，避免 tag 与 APP_VERSION 不一致）
  docVersions: {
    siteRoot: SITE_ROOT,
    current: DOC_VERSION,
    currentPath: VERSION_PATH,
    versions: DOC_VERSIONS,
  },
}

export default defineConfig({
  title: 'Sparks',
  description: 'M5Stack Cardputer multi-app firmware docs',
  base,
  // 多版本构建时由脚本传入绝对 outDir，避免互相覆盖
  outDir: process.env.DOCS_OUT_DIR || '.vitepress/dist',
  lastUpdated: true,
  cleanUrls: true,

  vite: {
    server: { port: 3123 },
    preview: { port: 3123 },
  },

  head: [
    ['link', { rel: 'icon', href: `${base}assets/logo_60.png` }],
  ],

  // md 中可用 {{APP_VERSION}} / {{APP_UPDATE_TIME}} / {{APP_AUTHOR}} / {{DOC_VERSION}}
  markdown: {
    config(md) {
      const render = md.render.bind(md)
      md.render = (src, env) =>
        render(
          src
            .replaceAll('{{APP_VERSION}}', APP_VERSION)
            .replaceAll('{{APP_UPDATE_TIME}}', APP_UPDATE_TIME)
            .replaceAll('{{APP_AUTHOR}}', APP_AUTHOR)
            .replaceAll('{{DOC_VERSION}}', DOC_VERSION),
          env,
        )
    },
  },

  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      description: 'M5Stack Cardputer 多应用固件文档',
      themeConfig: {
        ...sharedTheme,
        nav: [
          { text: '首页', link: '/' },
          { text: '功能目录', link: '/apps/' },
          { text: '截图', link: '/apps/shots' },
          { text: '快捷键', link: '/guide/shortcuts' },
          { component: 'VersionSwitcher' },
        ],
        sidebar: ARCHIVE_SIDEBARS?.root ?? [
          {
            text: '开始',
            items: [
              { text: '简介', link: '/' },
              { text: '入门', link: '/guide/getting-started' },
              { text: '全局快捷键', link: '/guide/shortcuts' },
            ],
          },
          {
            text: '功能目录',
            items: [
              { text: '总览', link: '/apps/' },
              { text: '截图总览', link: '/apps/shots' },
            ],
          },
          {
            text: '米家控制',
            items: [
              { text: 'Mijia 米家', link: '/apps/mijia' },
              { text: '米家设备 Token 获取', link: '/apps/mijia-token' },
              { text: 'Infrared 红外', link: '/apps/infrared' },
            ],
          },
          {
            text: '时间与电源',
            items: [
              { text: 'Time 时间', link: '/apps/time' },
              { text: 'Battery 电池', link: '/apps/battery' },
              { text: 'Sleep 睡眠', link: '/apps/sleep' },
              { text: 'Cursor 用量', link: '/apps/cursor' },
              { text: 'Keyboard', link: '/apps/hid-keyboard' },
              { text: 'Morse 摩斯', link: '/apps/morse' },
            ],
          },
          {
            text: '系统与信息',
            items: [
              { text: 'Config 配网', link: '/apps/config' },
              { text: 'WiFi', link: '/apps/wifi' },
              { text: 'Options 选项', link: '/apps/options' },
              { text: 'Info 信息', link: '/apps/info' },
              { text: 'Version 版本', link: '/apps/version' },
            ],
          },
          {
            text: '硬件调试与演示',
            items: [
              { text: 'Mic 麦克风', link: '/apps/mic' },
              {
                text: 'Mini Games 小游戏',
                link: '/apps/mini-games',
                collapsed: false,
                items: [
                  { text: 'Coin Toss 硬币', link: '/apps/coin-toss' },
                  { text: 'Double Pendulum 双摆', link: '/apps/double-pendulum' },
                  { text: 'Prize Wheel 抽奖轮', link: '/apps/prize-wheel' },
                  { text: 'Dice 骰子', link: '/apps/dice' },
                  { text: 'Newton Cradle 牛顿摆', link: '/apps/newton-cradle' },
                  { text: 'Neon FX 动画', link: '/apps/neon-fx' },
                  { text: 'Curves 方程曲线', link: '/apps/curves' },
                ],
              },
              {
                text: 'Hardware Test 硬件测试',
                link: '/apps/hardware-test',
                collapsed: false,
                items: [
                  { text: 'Display 显示', link: '/apps/display' },
                  { text: 'IMU', link: '/apps/imu' },
                  { text: 'Font 字体', link: '/apps/font' },
                  { text: 'Icons 图标', link: '/apps/icons' },
                  { text: 'RGB LED', link: '/apps/rgb-led' },
                  { text: 'BLE', link: '/apps/ble' },
                  { text: 'I2C 扫描', link: '/apps/i2c' },
                ],
              },
            ],
          },
          {
            text: '开发',
            items: [
              { text: '图片处理与烘焙', link: '/dev/images' },
              { text: '内存说明', link: '/dev/memory' },
            ],
          },
        ],
        outline: {
          label: '本页目录',
          level: [2, 3],
        },
        docFooter: {
          prev: '上一篇',
          next: '下一篇',
        },
        lastUpdated: {
          text: '最后更新',
        },
        returnToTopLabel: '返回顶部',
        sidebarMenuLabel: '菜单',
        darkModeSwitchLabel: '主题',
        lightModeSwitchTitle: '切换到浅色',
        darkModeSwitchTitle: '切换到深色',
        langMenuLabel: '切换语言',
      },
    },

    ...(HAS_EN_DOCS
      ? {
          en: {
      label: 'English',
      lang: 'en-US',
      description: 'M5Stack Cardputer multi-app firmware docs',
      themeConfig: {
        ...sharedTheme,
        nav: [
          { text: 'Home', link: '/en/' },
          { text: 'Apps', link: '/en/apps/' },
          { text: 'Screenshots', link: '/en/apps/shots' },
          { text: 'Shortcuts', link: '/en/guide/shortcuts' },
          { component: 'VersionSwitcher' },
        ],
        sidebar: ARCHIVE_SIDEBARS?.en ?? [
          {
            text: 'Start',
            items: [
              { text: 'Overview', link: '/en/' },
              { text: 'Getting Started', link: '/en/guide/getting-started' },
              { text: 'Global Shortcuts', link: '/en/guide/shortcuts' },
            ],
          },
          {
            text: 'App Catalog',
            items: [
              { text: 'Overview', link: '/en/apps/' },
              { text: 'Screenshot Gallery', link: '/en/apps/shots' },
            ],
          },
          {
            text: 'Mijia Control',
            items: [
              { text: 'Mijia', link: '/en/apps/mijia' },
              { text: 'Mijia Device Tokens', link: '/en/apps/mijia-token' },
              { text: 'Infrared', link: '/en/apps/infrared' },
            ],
          },
          {
            text: 'Time & Power',
            items: [
              { text: 'Time', link: '/en/apps/time' },
              { text: 'Battery', link: '/en/apps/battery' },
              { text: 'Sleep', link: '/en/apps/sleep' },
              { text: 'Cursor Usage', link: '/en/apps/cursor' },
              { text: 'Keyboard', link: '/en/apps/hid-keyboard' },
              { text: 'Morse', link: '/en/apps/morse' },
            ],
          },
          {
            text: 'System & Info',
            items: [
              { text: 'Config', link: '/en/apps/config' },
              { text: 'WiFi', link: '/en/apps/wifi' },
              { text: 'Options', link: '/en/apps/options' },
              { text: 'Info', link: '/en/apps/info' },
              { text: 'Version', link: '/en/apps/version' },
            ],
          },
          {
            text: 'Hardware Demos',
            items: [
              { text: 'Mic', link: '/en/apps/mic' },
              {
                text: 'Mini Games',
                link: '/en/apps/mini-games',
                collapsed: false,
                items: [
                  { text: 'Coin Toss', link: '/en/apps/coin-toss' },
                  { text: 'Double Pendulum', link: '/en/apps/double-pendulum' },
                  { text: 'Prize Wheel', link: '/en/apps/prize-wheel' },
                  { text: 'Dice', link: '/en/apps/dice' },
                  { text: 'Newton Cradle', link: '/en/apps/newton-cradle' },
                  { text: 'Neon FX', link: '/en/apps/neon-fx' },
                  { text: 'Curves', link: '/en/apps/curves' },
                ],
              },
              {
                text: 'Hardware Test',
                link: '/en/apps/hardware-test',
                collapsed: false,
                items: [
                  { text: 'Display', link: '/en/apps/display' },
                  { text: 'IMU', link: '/en/apps/imu' },
                  { text: 'Font', link: '/en/apps/font' },
                  { text: 'Icons', link: '/en/apps/icons' },
                  { text: 'RGB LED', link: '/en/apps/rgb-led' },
                  { text: 'BLE', link: '/en/apps/ble' },
                  { text: 'I2C Scan', link: '/en/apps/i2c' },
                ],
              },
            ],
          },
          {
            text: 'Development',
            items: [
              { text: 'Images & RGB565 Bake', link: '/en/dev/images' },
              { text: 'Memory Notes', link: '/en/dev/memory' },
            ],
          },
        ],
        outline: {
          label: 'On this page',
          level: [2, 3],
        },
        docFooter: {
          prev: 'Previous',
          next: 'Next',
        },
        lastUpdated: {
          text: 'Last updated',
        },
        returnToTopLabel: 'Return to top',
        sidebarMenuLabel: 'Menu',
        darkModeSwitchLabel: 'Theme',
        lightModeSwitchTitle: 'Switch to light',
        darkModeSwitchTitle: 'Switch to dark',
        langMenuLabel: 'Change language',
      },
    },
        }
      : {}),
  },
})

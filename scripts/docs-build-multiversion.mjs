/**
 * 多版本文档构建：最新版 → dist/，历史 git tag → dist/v/{slug}/
 * 仅构建含 docs/.vitepress/config.mts 的 v* tag。
 */
import { execSync, execFileSync } from 'node:child_process'
import {
  cpSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const ROOT = resolve(__dirname, '..')
const DOCS = join(ROOT, 'docs')
const VP = join(DOCS, '.vitepress')
const DIST = join(VP, 'dist')
const STAGING = join(VP, '.version-builds')
const WORKTREES = join(ROOT, '.docs-worktrees')
const VERSIONS_JSON = join(VP, 'versions.generated.json')

const SITE_ROOT = process.env.GITHUB_ACTIONS ? '/m5stack-cardputer-sparks' : ''
const VERSION_LIMIT = Number(process.env.DOCS_VERSION_LIMIT || 20)

function sh(cmd, opts = {}) {
  console.log(`$ ${cmd}`)
  return execSync(cmd, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'inherit'],
    ...opts,
  })
}

function shOut(cmd) {
  return sh(cmd, { stdio: ['pipe', 'pipe', 'pipe'] }).trim()
}

function readAppVersion(repoRoot) {
  const h = readFileSync(join(repoRoot, 'include/app_version.h'), 'utf8')
  const m = h.match(/APP_VERSION\s*=\s*"([^"]+)"/)
  return m?.[1] ? `v${m[1]}` : 'v0.0'
}

/** tag → URL 路径段：v1.02 → 1.02；v0.10(beta) → 0.10-beta（最终路径 /v/1.02/） */
function slugifyTag(tag) {
  return tag
    .replace(/^v/i, '')
    .replace(/\(/g, '-')
    .replace(/\)/g, '')
    .replace(/[^\w.-]+/g, '-')
}

function tagHasDocs(tag) {
  try {
    // 整段 object 需一起引号，避免 v0.10(beta) 等 tag 被 shell 拆开
    shOut(`git cat-file -e ${JSON.stringify(`${tag}:docs/.vitepress/config.mts`)}`)
    return true
  } catch {
    return false
  }
}

function listDocTags() {
  let raw = ''
  try {
    raw = shOut('git tag -l "v*" --sort=-v:refname')
  } catch {
    return []
  }
  const candidates = raw
    .split(/\r?\n/)
    .map((t) => t.trim())
    .filter(Boolean)
    .filter((t) => /^v\d/.test(t))

  const result = []
  for (const tag of candidates) {
    if (result.length >= VERSION_LIMIT) break
    if (!tagHasDocs(tag)) continue
    result.push({ tag, slug: slugifyTag(tag) })
  }
  return result
}

function writeVersionsFile(versions) {
  writeFileSync(VERSIONS_JSON, JSON.stringify(versions, null, 2) + '\n', 'utf8')
}

function runVitepressBuild({ docsDir, versionPath, outDir }) {
  mkdirSync(outDir, { recursive: true })
  const env = {
    ...process.env,
    DOCS_VERSION_PATH: versionPath,
    DOCS_OUT_DIR: outDir,
  }
  if (process.env.GITHUB_ACTIONS) env.GITHUB_ACTIONS = 'true'

  // 走 js 入口，避免 Windows 下 .cmd + execFileSync 问题
  const entry = join(ROOT, 'node_modules', 'vitepress', 'bin', 'vitepress.js')
  console.log(`→ vitepress build ${docsDir} (path=${versionPath || 'latest'} → ${outDir})`)
  execFileSync(process.execPath, [entry, 'build', docsDir], {
    cwd: ROOT,
    env,
    stdio: 'inherit',
  })
}

/** 用当前主题/配置覆盖 worktree，保留该 tag 的 md 与 app_version.h */
function overlayBuildTooling(worktree) {
  const dstVp = join(worktree, 'docs', '.vitepress')
  mkdirSync(dstVp, { recursive: true })
  cpSync(join(VP, 'config.mts'), join(dstVp, 'config.mts'))
  cpSync(join(VP, 'theme'), join(dstVp, 'theme'), { recursive: true })
  if (existsSync(VERSIONS_JSON)) {
    cpSync(VERSIONS_JSON, join(dstVp, 'versions.generated.json'))
  }
}

function ensureWorktree(tag, slug) {
  mkdirSync(WORKTREES, { recursive: true })
  const dir = join(WORKTREES, slug)
  // 清理可能残留的同名目录（首次 remove 失败可忽略）
  try {
    sh(`git worktree remove -f ${JSON.stringify(dir)}`, { stdio: 'ignore' })
  } catch {
    /* 可能尚不存在 */
  }
  if (existsSync(dir)) rmSync(dir, { recursive: true, force: true })
  sh(`git worktree add --detach ${JSON.stringify(dir)} ${JSON.stringify(tag)}`)
  return dir
}

function writeVersionsIndex(distDir, versions) {
  const dir = join(distDir, 'v')
  mkdirSync(dir, { recursive: true })
  const items = versions
    .map((v) => {
      const href = v.latest || !v.path ? `${SITE_ROOT || ''}/` : `${SITE_ROOT}/${v.path}/`
      return `<li><a href="${href}">${v.label}</a></li>`
    })
    .join('\n')
  const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Sparks Docs — Versions</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 40rem; margin: 3rem auto; padding: 0 1rem; }
    a { color: #3451b2; }
  </style>
</head>
<body>
  <h1>Documentation versions</h1>
  <ul>
${items}
  </ul>
</body>
</html>
`
  writeFileSync(join(dir, 'index.html'), html, 'utf8')
}

function main() {
  const currentVersion = readAppVersion(ROOT)
  const tags = listDocTags()
  console.log(`Current firmware docs: ${currentVersion}`)
  console.log(`Archived tags (${tags.length}): ${tags.map((t) => t.tag).join(', ') || '(none)'}`)

  const versions = [
    {
      label: `${currentVersion} (latest)`,
      version: currentVersion,
      path: '',
      latest: true,
    },
    ...tags.map(({ tag, slug }) => ({
      label: tag,
      // 与该 tag 构建时 DOC_VERSION（来自 app_version.h）对齐，供切换器高亮
      version: tag.startsWith('v') ? tag : `v${slug}`,
      path: `v/${slug}`,
      latest: false,
      tag,
    })),
  ]

  // 若某 tag slug 与 current 显示名相同，归档项仍保留（路径 /v/...），latest 在根
  writeVersionsFile(versions)

  rmSync(STAGING, { recursive: true, force: true })
  mkdirSync(STAGING, { recursive: true })

  // 1) 最新版（当前工作树）
  const latestOut = join(STAGING, '_latest')
  runVitepressBuild({ docsDir: DOCS, versionPath: '', outDir: latestOut })

  // 2) 历史 tag
  for (const { tag, slug } of tags) {
    const worktree = ensureWorktree(tag, slug)
    try {
      overlayBuildTooling(worktree)
      const outDir = join(STAGING, slug)
      runVitepressBuild({
        docsDir: join(worktree, 'docs'),
        versionPath: `v/${slug}`,
        outDir,
      })
    } finally {
      try {
        sh(`git worktree remove -f ${JSON.stringify(worktree)}`)
      } catch {
        rmSync(worktree, { recursive: true, force: true })
      }
    }
  }

  // 3) 组装 dist
  rmSync(DIST, { recursive: true, force: true })
  mkdirSync(DIST, { recursive: true })
  cpSync(latestOut, DIST, { recursive: true })

  for (const { slug } of tags) {
    const src = join(STAGING, slug)
    if (!existsSync(src)) continue
    const dst = join(DIST, 'v', slug)
    mkdirSync(dirname(dst), { recursive: true })
    cpSync(src, dst, { recursive: true })
  }

  writeFileSync(join(DIST, 'versions.json'), JSON.stringify(versions, null, 2) + '\n')
  writeVersionsIndex(DIST, versions)

  // 清理 staging / 残留 worktree 目录
  rmSync(STAGING, { recursive: true, force: true })
  if (existsSync(WORKTREES)) {
    try {
      rmSync(WORKTREES, { recursive: true, force: true })
    } catch {
      /* Windows 偶发占用可忽略 */
    }
  }

  console.log(`\nDone. Output: ${DIST}`)
  console.log(`  latest → ${SITE_ROOT || '/'}`)
  for (const { slug } of tags) {
    console.log(`  ${slug} → ${SITE_ROOT}/v/${slug}/`)
  }
}

main()

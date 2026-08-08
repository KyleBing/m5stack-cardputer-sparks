/**
 * 多版本文档构建：最新版 → dist/，历史 git tag → dist/v/{slug}/
 *
 * 归档版：保留该 tag 的 markdown，使用当前 VitePress 工具链；
 * 侧栏从该 tag 的 config.mts 提取后写入 sidebars.generated.json，
 * 由当前 config 读取，确保左侧导航与版本内容一致。
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

/** tag → URL 路径段：v1.02 → 1.02；v0.10(beta) → 0.10-beta */
function slugifyTag(tag) {
  return tag
    .replace(/^v/i, '')
    .replace(/\(/g, '-')
    .replace(/\)/g, '')
    .replace(/[^\w.-]+/g, '-')
}

function tagHasDocs(tag) {
  try {
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

  const entry = join(ROOT, 'node_modules', 'vitepress', 'bin', 'vitepress.js')
  console.log(`→ vitepress build ${docsDir} (path=${versionPath || 'latest'} → ${outDir})`)
  execFileSync(process.execPath, [entry, 'build', docsDir], {
    cwd: ROOT,
    env,
    stdio: 'inherit',
  })
}

function skipString(src, i) {
  const q = src[i]
  i++
  while (i < src.length) {
    if (src[i] === '\\') {
      i += 2
      continue
    }
    if (src[i] === q) return i + 1
    i++
  }
  return i
}

/**
 * 提取所有 sidebar 数组字面量（按出现顺序）。
 * 兼容 `sidebar: [...]` 与 `sidebar: ARCHIVE_SIDEBARS?.root ?? [...]`。
 */
function extractSidebarLiterals(src) {
  const found = []
  let searchFrom = 0
  while (searchFrom < src.length) {
    // 勿匹配 sidebarMenuLabel；允许 ?? 等表达式后再接数组
    const idx = src.slice(searchFrom).search(/\bsidebar\s*:/)
    if (idx < 0) break
    const abs = searchFrom + idx
    const afterColon = src.indexOf(':', abs) + 1
    const bracketAt = src.indexOf('[', afterColon)
    if (bracketAt < 0) break

    let i = bracketAt
    let depth = 0
    while (i < src.length) {
      const c = src[i]
      if (c === '"' || c === "'" || c === '`') {
        i = skipString(src, i)
        continue
      }
      if (c === '[') depth++
      else if (c === ']') {
        depth--
        if (depth === 0) {
          found.push(src.slice(bracketAt, i + 1))
          searchFrom = i + 1
          break
        }
      }
      i++
    }
    if (i >= src.length) break
  }
  return found
}

/** 从 tag 的 config 提取侧栏，写成 sidebars.generated.json */
function extractSidebarsFromConfig(configPath) {
  const src = readFileSync(configPath, 'utf8')
  const literals = extractSidebarLiterals(src)
  if (literals.length === 0) {
    throw new Error(`No sidebar found in ${configPath}`)
  }

  const evalLiteral = (lit) => {
    // 侧栏为纯数据字面量，可安全求值
    return new Function(`"use strict"; return (${lit})`)()
  }

  // locales：通常 root 在前、en 在后；扁平 config 只有一个
  const root = evalLiteral(literals[0])
  const en = literals.length > 1 ? evalLiteral(literals[1]) : null
  return { root, en }
}

/**
 * 归档构建准备：当前 config/theme + 该版侧栏 JSON + versions JSON。
 * 不覆盖 markdown / app_version.h。
 */
function overlayBuildTooling(worktree) {
  const dstVp = join(worktree, 'docs', '.vitepress')
  const tagConfig = join(dstVp, 'config.mts')
  mkdirSync(dstVp, { recursive: true })

  const sidebars = extractSidebarsFromConfig(tagConfig)
  writeFileSync(join(dstVp, 'sidebars.generated.json'), JSON.stringify(sidebars, null, 2) + '\n')

  cpSync(join(VP, 'config.mts'), join(dstVp, 'config.mts'))
  cpSync(join(VP, 'theme'), join(dstVp, 'theme'), { recursive: true })
  if (existsSync(VERSIONS_JSON)) {
    cpSync(VERSIONS_JSON, join(dstVp, 'versions.generated.json'))
  }

  console.log(
    `  sidebars: root=${sidebars.root?.length ?? 0} groups` +
      (sidebars.en ? `, en=${sidebars.en.length} groups` : ' (no en)'),
  )
}

function ensureWorktree(tag, slug) {
  mkdirSync(WORKTREES, { recursive: true })
  const dir = join(WORKTREES, slug)
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
      version: tag.startsWith('v') ? tag : `v${slug}`,
      path: `v/${slug}`,
      latest: false,
      tag,
    })),
  ]

  writeVersionsFile(versions)

  rmSync(STAGING, { recursive: true, force: true })
  mkdirSync(STAGING, { recursive: true })

  // 最新版：清掉可能残留的归档侧栏覆盖
  const latestSidebars = join(VP, 'sidebars.generated.json')
  if (existsSync(latestSidebars)) rmSync(latestSidebars)

  const latestOut = join(STAGING, '_latest')
  runVitepressBuild({ docsDir: DOCS, versionPath: '', outDir: latestOut })

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
      if (!existsSync(join(outDir, 'index.html'))) {
        throw new Error(`Archive build produced no index.html: ${outDir}`)
      }
    } finally {
      try {
        sh(`git worktree remove -f ${JSON.stringify(worktree)}`)
      } catch {
        rmSync(worktree, { recursive: true, force: true })
      }
    }
  }

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

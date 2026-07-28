<script setup lang="ts">
import { computed, ref, onMounted, onUnmounted } from 'vue'
import { useData } from 'vitepress'

type DocVersion = {
  label: string
  version: string
  /** 相对站点根的路径，最新版为空；历史版如 v/1.02 */
  path: string
  latest?: boolean
}

const { theme, isDark, lang } = useData()
const open = ref(false)

const docVersions = computed(() => {
  const cfg = (theme.value as {
    docVersions?: {
      siteRoot?: string
      current?: string
      currentPath?: string
      versions?: DocVersion[]
    }
  }).docVersions
  return {
    siteRoot: cfg?.siteRoot ?? '',
    current: cfg?.current ?? '',
    currentPath: cfg?.currentPath ?? '',
    versions: cfg?.versions ?? [],
  }
})

const currentLabel = computed(() => {
  const { currentPath, versions } = docVersions.value
  const hit =
    versions.find((v) => (v.path || '') === currentPath) ??
    versions.find((v) => v.latest)
  return hit?.label ?? docVersions.value.current
})

function isActive(v: DocVersion) {
  return (v.path || '') === docVersions.value.currentPath
}

function homePath() {
  return lang.value.startsWith('en') ? '/en/' : '/'
}

function hrefFor(v: DocVersion) {
  const root = docVersions.value.siteRoot.replace(/\/$/, '')
  const home = homePath()
  if (v.latest || !v.path) {
    return `${root}${home}` || home
  }
  const seg = v.path.replace(/^\/|\/$/g, '')
  return `${root}/${seg}${home}`
}

function go(v: DocVersion) {
  open.value = false
  const url = hrefFor(v)
  if (typeof window !== 'undefined') {
    window.location.href = url
  }
}

function onDocClick(e: MouseEvent) {
  const t = e.target as HTMLElement | null
  if (!t?.closest?.('.doc-version-switcher')) open.value = false
}

onMounted(() => document.addEventListener('click', onDocClick))
onUnmounted(() => document.removeEventListener('click', onDocClick))
</script>

<template>
  <div v-if="docVersions.versions.length > 0" class="doc-version-switcher" :class="{ open }">
    <button
      type="button"
      class="doc-version-btn"
      :aria-expanded="open"
      aria-haspopup="listbox"
      @click.stop="open = !open"
    >
      {{ currentLabel }}
      <span class="doc-version-caret" aria-hidden="true">▾</span>
    </button>
    <ul v-show="open" class="doc-version-menu" role="listbox" :data-theme="isDark ? 'dark' : 'light'">
      <li v-for="v in docVersions.versions" :key="v.version + v.path">
        <button
          type="button"
          role="option"
          :aria-selected="isActive(v)"
          :class="{ active: isActive(v) }"
          @click="go(v)"
        >
          {{ v.label }}
        </button>
      </li>
    </ul>
  </div>
</template>

<style scoped>
.doc-version-switcher {
  position: relative;
  display: flex;
  align-items: center;
}

.doc-version-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 0 8px;
  height: var(--vp-nav-height);
  border: 0;
  background: transparent;
  color: var(--vp-c-text-1);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
}

.doc-version-btn:hover {
  color: var(--vp-c-brand-1);
}

.doc-version-caret {
  font-size: 10px;
  opacity: 0.7;
}

.doc-version-menu {
  position: absolute;
  top: calc(var(--vp-nav-height) - 8px);
  right: 0;
  z-index: 100;
  min-width: 140px;
  margin: 0;
  padding: 6px;
  list-style: none;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  background: var(--vp-c-bg-elv);
  box-shadow: var(--vp-shadow-3);
}

.doc-version-menu button {
  display: block;
  width: 100%;
  padding: 6px 10px;
  border: 0;
  border-radius: 4px;
  background: transparent;
  color: var(--vp-c-text-1);
  font-size: 13px;
  text-align: left;
  cursor: pointer;
}

.doc-version-menu button:hover,
.doc-version-menu button.active {
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-brand-1);
}
</style>

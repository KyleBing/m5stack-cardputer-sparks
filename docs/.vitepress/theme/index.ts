import DefaultTheme from 'vitepress/theme'
import VersionSwitcher from './VersionSwitcher.vue'
import './custom.css'
import type { EnhanceAppContext } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }: EnhanceAppContext) {
    app.component('VersionSwitcher', VersionSwitcher)
  },
}

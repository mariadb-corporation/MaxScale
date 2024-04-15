/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { fileURLToPath, URL } from 'node:url'
import fs from 'fs'
import { defineConfig, loadEnv, splitVendorChunkPlugin } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify, { transformAssetUrls } from 'vite-plugin-vuetify'
import autoImport from 'unplugin-auto-import/vite'
import components from 'unplugin-vue-components/vite'
import legacy from '@vitejs/plugin-legacy'

const { VITE_APP_API, VITE_HTTPS_KEY, VITE_HTTPS_CERT, VITE_OUT_DIR } = loadEnv(
  'development',
  process.cwd()
)

const protocol = VITE_APP_API.startsWith('https') ? 'wss' : 'ws'
const wsTarget = `${protocol}://${VITE_APP_API.replace(/^https?:\/\//, '')}`

export default defineConfig({
  plugins: [
    vue({ template: { transformAssetUrls } }),
    components({ dirs: ['src/components/common'], dts: false }),
    vuetify({ styles: { configFile: 'src/styles/variables/vuetify.scss' } }),
    autoImport({
      imports: ['vue', 'vitest', 'vuex', 'vue-i18n', 'vue-router'],
      dts: false,
      dirs: ['src/composables/common'],
      eslintrc: { enabled: true, filepath: './.eslintrc-auto-import.json', globalsPropValue: true },
    }),
    legacy({ targets: ['defaults', 'not IE 11'] }), // required terser package
    splitVendorChunkPlugin(),
  ],
  css: {
    preprocessorOptions: {
      scss: {
        additionalData: `
        @use '@/styles/variables/vuetify.scss' as vuetifyVar; 
        @use '@/styles/variables/colors.scss' as colors;`,
      },
    },
  },
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
      '@wsModels': fileURLToPath(new URL('./src/store/orm/models', import.meta.url)),
      '@wsServices': fileURLToPath(new URL('./src/services/workspace', import.meta.url)),
      '@wsComps': fileURLToPath(new URL('./src/components/workspace', import.meta.url)),
      '@wkeComps': fileURLToPath(new URL('./src/components/workspace/worksheets', import.meta.url)),
    },
  },
  server: {
    port: 3000,
    ...(VITE_HTTPS_KEY && VITE_HTTPS_CERT
      ? { https: { key: fs.readFileSync(VITE_HTTPS_KEY), cert: fs.readFileSync(VITE_HTTPS_CERT) } }
      : {}),
    proxy: {
      '/api': {
        target: VITE_APP_API,
        changeOrigin: true,
        secure: false,
        rewrite: (path) => path.replace('/api', ''),
      },
      '/maxscale/logs/stream': {
        target: wsTarget,
        changeOrigin: true,
        secure: false,
        ws: true,
        rewrite: (path) => path.replace(/^\/maxscale\/logs\/stream/, '/maxscale/logs/stream'),
      },
    },
  },
  esbuild: { pure: ['console.log'] },
  build: {
    outDir: `${VITE_OUT_DIR}/gui`,
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        manualChunks: (id) => {
          const monacoMatch = id.match(/monaco-editor\/esm\/vs\/(base|editor|platform)\/(.*)/)
          if (monacoMatch) {
            const baseName = monacoMatch[1]
            if (baseName === 'base' || baseName === 'editor') {
              let moduleName = monacoMatch[2].split('/')[0] // Extract the top-level module name
              // For modules in `/editor`, especially 'common' and 'contrib', which are larger than 500KB,
              // it's beneficial to split them into smaller chunks by two levels of modules
              if (baseName === 'editor' && (moduleName === 'common' || moduleName === 'contrib'))
                moduleName = monacoMatch[2].split('/').slice(0, 2).join('/')
              return `monaco-editor/${baseName}/${moduleName}`
            }
            return `monaco-editor/${baseName}`
          }
          // Return the chunk name based on the baseName and moduleName
          if (id.includes('node_modules'))
            return id.toString().split('node_modules/')[1].split('/')[0].toString()
        },
      },
    },
  },
})

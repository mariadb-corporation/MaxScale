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
import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify, { transformAssetUrls } from 'vite-plugin-vuetify'
import autoImport from 'unplugin-auto-import/vite'
import components from 'unplugin-vue-components/vite'
import legacy from '@vitejs/plugin-legacy'

const { VITE_APP_API, VITE_HTTPS_KEY, VITE_HTTPS_CERT } = loadEnv('development', process.cwd())

export default defineConfig({
  plugins: [
    vue({ template: { transformAssetUrls } }),
    components({ dirs: ['src/components/common'], dts: false }),
    vuetify({
      styles: {
        configFile: 'src/styles/variables/vuetify.scss',
      },
    }),
    autoImport({
      imports: ['vue', 'vitest', 'vuex', 'vue-i18n', 'vue-router'],
      dts: false,
      dirs: ['src/composables'],
      eslintrc: {
        enabled: true,
        filepath: './.eslintrc-auto-import.json',
        globalsPropValue: true,
      },
    }),
    legacy({
      targets: ['defaults', 'not IE 11'], // required terser package
    }),
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
    },
  },
  esbuild: {
    pure: ['console.log'],
  },
})

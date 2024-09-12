/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { fileURLToPath } from 'node:url'
import { mergeConfig, defineConfig, configDefaults } from 'vitest/config'
import viteConfig from './vite.config'

export default mergeConfig(
  viteConfig,
  defineConfig({
    test: {
      setupFiles: ['src/tests/setup.js'],
      environment: 'happy-dom',
      exclude: [...configDefaults.exclude, 'e2e/*'],
      root: fileURLToPath(new URL('./', import.meta.url)),
      globals: true,
      server: { deps: { inline: ['vuetify'] } },
      coverage: {
        provider: 'istanbul',
        reporter: ['text', 'html'],
        include: [
          'src/components/**/*.{js,vue}',
          'src/composables/**/*.{js,vue}',
          'src/layouts/**/*.{js,vue}',
          'src/services/**/*.{js,vue}',
          'src/utils/**/*.{js,vue}',
        ],
        exclude: ['**/__tests__/**'],
        all: true,
      },
    },
  })
)

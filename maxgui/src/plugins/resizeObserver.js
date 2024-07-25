/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@/utils/helpers'

export default {
  install: (app) => {
    app.directive('resize-observer', {
      mounted(el, binding) {
        let width = 0,
          height = 0

        const updateSize = lodash.debounce((entries) => {
          for (const entry of entries) {
            width = entry.contentRect.width
            height = entry.contentRect.height
          }
          if (binding.value) binding.value({ width, height })
        }, 200)

        const resizeObserver = new ResizeObserver(updateSize)
        resizeObserver.observe(el)
        el._resizeObserver = resizeObserver
      },
      unmounted(el) {
        if (el._resizeObserver) {
          el._resizeObserver.disconnect()
        }
      },
    })
  },
}

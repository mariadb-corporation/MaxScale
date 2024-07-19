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
import { t as typy } from 'typy'

/**
 * @param {object} data - proxy object
 */
function useCommonResSetAttrs(data) {
  const isLoading = computed(() => typy(data.value, 'is_loading').safeBoolean)
  const startTime = computed(() => typy(data.value, 'start_time').safeNumber)
  const execTime = computed(() => {
    if (isLoading.value) return -1
    const execution_time = typy(data.value, 'data.attributes.execution_time').safeNumber
    if (execution_time) return parseFloat(execution_time.toFixed(4))
    return 0
  })
  const endTime = computed(() => typy(data.value, 'end_time').safeNumber)
  return {
    isLoading,
    startTime,
    execTime,
    endTime,
  }
}

export default { useCommonResSetAttrs }

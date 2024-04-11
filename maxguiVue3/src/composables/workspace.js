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
import { t as typy } from 'typy'
/**
 * @param {object} data - proxy object
 */
export function useCommonResSetAttrs(data) {
  const isLoading = computed(() => typy(data.value, 'is_loading').safeBoolean)
  const requestSentTime = computed(() => typy(data.value, 'request_sent_time').safeNumber)
  const execTime = computed(() => {
    if (isLoading.value) return -1
    const execution_time = typy(data.value, 'data.attributes.execution_time').safeNumber
    if (execution_time) return parseFloat(execution_time.toFixed(4))
    return 0
  })
  const totalDuration = computed(() => typy(data.value, 'total_duration').safeNumber)
  return {
    isLoading,
    requestSentTime,
    execTime,
    totalDuration,
  }
}

<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Scatter as ScatterChart } from 'vue-chartjs'
import { mergeBaseOpts } from '@/components/common/Charts/utils'

const props = defineProps({ opts: { type: Object, default: () => {} } })
const {
  lodash: { merge },
} = useHelpers()

const options = computed(() =>
  merge(
    {
      scales: {
        x: { type: 'linear', position: 'bottom', beginAtZero: true },
        y: { beginAtZero: true },
      },
    },
    mergeBaseOpts(props.opts)
  )
)
</script>

<template>
  <ScatterChart :style="{ width: '100%' }" :options="options" />
</template>

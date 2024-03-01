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
import { datasetObjectTooltip } from '@/components/common/Charts/customTooltips.js'
import { onUnmounted } from 'vue'

const props = defineProps({ resTimeDist: { type: Object, required: true } })

const { uuidv1, dynamicColors, strReplaceAt } = useHelpers()
const typy = useTypy()

const uniqueTooltipId = `chartTooltip_${uuidv1()}`
const parsing = { xAxisKey: 'time', yAxisKey: 'count' }
const chart = ref(null)
const labels = computed(() =>
  typy(props.resTimeDist, 'read.distribution').safeArray.map((item) => item.time)
)

const data = computed(() => {
  return {
    datasets: [
      {
        label: 'Read',
        data: typy(props.resTimeDist, 'read.distribution').safeArray,
        ...genDatasetStyleProperties(0),
      },
      {
        label: 'Write',
        data: typy(props.resTimeDist, 'write.distribution').safeArray,
        ...genDatasetStyleProperties(1),
      },
    ],
  }
})
const chartOptions = computed(() => ({
  layout: { padding: { left: 8, bottom: 8, right: 0, top: 8 } },
  parsing: parsing,
  scales: {
    x: {
      title: { display: true, text: 'Time (sec)', font: { size: 14 } },
      ticks: { callback: (tick) => parseFloat(labels.value[tick]) },
    },
    y: {
      title: { display: true, text: 'Count' },
      ticks: { callback: (tick) => tick },
    },
  },
  plugins: {
    legend: { display: true },
    tooltip: {
      external: (context) =>
        datasetObjectTooltip({
          context,
          tooltipId: uniqueTooltipId,
          parsing: parsing,
          alignTooltipToLeft: context.tooltip.caretX >= chart.value.$el.clientWidth / 2,
        }),
    },
  },
}))

onUnmounted(() => removeTooltip())

function removeTooltip() {
  let tooltipEl = document.getElementById(uniqueTooltipId)
  if (tooltipEl) tooltipEl.remove()
}

function genDatasetStyleProperties(colorIdx = 0) {
  const lineColor = dynamicColors(colorIdx)
  const indexOfOpacity = lineColor.lastIndexOf(')') - 1
  const backgroundColor = strReplaceAt({
    str: lineColor,
    index: indexOfOpacity,
    newChar: '0.2',
  })
  return {
    backgroundColor,
    borderColor: lineColor,
    hoverBackgroundColor: lineColor,
    borderWidth: 1,
    minBarLength: 0,
  }
}
</script>

<template>
  <CollapsibleCtr :title="$t('resTimeDist')">
    <template #title-append>
      <VMenu
        open-on-hover
        location="top"
        max-width="400"
        content-class="shadow-drop text-navigation rounded-sm"
        :close-delay="300"
      >
        <template #activator="{ props }">
          <VIcon
            class="ml-1 material-icons-outlined pointer"
            size="18"
            color="info"
            icon="$mdiInformationOutline"
            v-bind="props"
          />
        </template>
        <i18n-t
          keypath="info.resTimeDist"
          tag="div"
          class="bg-background py-2 px-4 text-body-2"
          scope="global"
        >
          <template #default>
            <a
              target="_blank"
              rel="noopener noreferrer"
              href="https://mariadb.com/kb/en/query-response-time-plugin/"
            >
              Query Response Time
            </a>
          </template>
        </i18n-t>
      </VMenu>
    </template>
    <!-- The height of the chart is 535 which equals to the height
        of the STATISTICS table. This makes the UI look balanced.
     -->
    <VCard flat border :height="535">
      <BarChart ref="chart" class="fill-height" :data="data" :opts="chartOptions" />
    </VCard>
  </CollapsibleCtr>
</template>

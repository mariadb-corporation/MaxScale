<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTasks from '@wkeComps/BlankWke/EtlTasks.vue'

const props = defineProps({
  ctrDim: { type: Object, required: true },
  cards: { type: Array, required: true },
})

let taskCardCtrHeight = ref(200)
let taskCardRef = ref(null)

const migrationTaskTblHeight = computed(
  () => props.ctrDim.height - taskCardCtrHeight.value - 12 - 24 - 80 // minus grid padding
)
const hasEtlTasks = computed(() => Boolean(EtlTask.all().length))

onMounted(() => setTaskCardCtrHeight())

function setTaskCardCtrHeight() {
  const { height } = taskCardRef.value.$el.getBoundingClientRect()
  taskCardCtrHeight.value = height
}
</script>

<template>
  <VContainer class="blank-wke-ctr">
    <VRow ref="taskCardRef" class="task-card-ctr" justify="center">
      <VCol cols="12" class="d-flex justify-center pa-0">
        <h6 class="text-navigation font-weight-regular text-h6">
          {{ $t('chooseTask') }}
        </h6>
      </VCol>
      <VCol cols="12" class="d-flex flex-row flex-wrap justify-center py-2">
        <VCard
          v-for="(card, i) in cards"
          :key="i"
          flat
          border
          class="ma-2 px-2 rounded-lg task-card pos--relative"
          :class="{ 'border--separator': card.disabled }"
          height="90"
          width="225"
          :disabled="card.disabled"
          @click="card.click"
        >
          <div
            class="d-flex fill-height align-center justify-center card-title"
            :class="card.disabled ? 'text-grayed-out' : 'text-primary'"
          >
            <VIcon
              :size="card.iconSize"
              :color="card.disabled ? 'grayed-out' : 'primary'"
              class="mr-4"
              :icon="card.icon"
            />
            <div class="d-flex flex-column">
              {{ card.title }}
              <span class="card-subtitle font-weight-medium">
                {{ card.subtitle }}
              </span>
            </div>
          </div>
        </VCard>
      </VCol>
      <VCol cols="12" class="d-flex justify-center pa-0">
        <slot name="blank-worksheet-task-cards-bottom" />
      </VCol>
    </VRow>
    <VRow v-if="hasEtlTasks" justify="center">
      <VCol cols="12" md="10">
        <h6 class="text-navigation font-weight-regular text-h6">
          {{ $t('migrationTasks') }}
        </h6>
        <EtlTasks :height="migrationTaskTblHeight" />
      </VCol>
    </VRow>
  </VContainer>
</template>

<style lang="scss" scoped>
.blank-wke-ctr {
  .task-card-ctr {
    padding-top: 90px;
    .task-card {
      border-color: #bed1da;
      .card-title {
        font-size: 1.125rem;
        opacity: 1;
        .card-subtitle {
          font-size: 0.75rem;
        }
      }
    }
  }
}
</style>

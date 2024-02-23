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
import AnnotationsCnfCtr from '@/components/dashboard/AnnotationsCnfCtr.vue'

const attrs = useAttrs()

const { lodash } = useHelpers()
const store = useStore()

const dsh_graphs_cnf = computed(() => store.state.persisted.dsh_graphs_cnf)
const hasChanged = computed(() => !lodash.isEqual(dsh_graphs_cnf.value, graphsCnf.value))

let activeGraphName = ref('load')
let graphsCnf = ref({})

function onSave() {
  store.commit('persisted/SET_DSH_GRAPHS_CNF', lodash.cloneDeep(graphsCnf.value), { root: true })
}

watch(
  () => attrs.modelValue,
  (v) => {
    if (v) graphsCnf.value = lodash.cloneDeep(dsh_graphs_cnf.value)
    else graphsCnf.value = {}
  },
  { immediate: true }
)
</script>

<template>
  <BaseDlg
    :onSave="onSave"
    :title="$t('configuration')"
    :lazyValidation="false"
    :hasChanged="hasChanged"
    minBodyWidth="800px"
    bodyCtrClass="px-0 pb-4"
    formClass="px-10 py-0"
  >
    <template #form-body>
      <div class="d-flex">
        <VTabs v-model="activeGraphName" direction="vertical" class="v-tabs--vert" hide-slider>
          <VTab v-for="(_, key) in dsh_graphs_cnf" :value="key" :key="key">
            <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
              {{ key }}
            </div>
          </VTab>
        </VTabs>
        <VWindow v-model="activeGraphName" class="w-100">
          <VWindowItem :value="activeGraphName" class="w-100">
            <div class="d-flex pl-4 pr-2 overflow-y-auto graph-cnf-ctr w-100">
              <div
                v-for="(data, cnfType) in graphsCnf[activeGraphName]"
                :key="cnfType"
                class="fill-height overflow-hidden w-100"
              >
                <AnnotationsCnfCtr
                  v-if="cnfType === 'annotations'"
                  v-model="graphsCnf[activeGraphName][cnfType]"
                  :cnfType="cnfType"
                />
              </div>
            </div>
          </VWindowItem>
        </VWindow>
      </div>
    </template>
  </BaseDlg>
</template>

<style lang="scss" scoped>
.graph-cnf-ctr {
  min-height: 360px;
  max-height: 420px;
}
</style>

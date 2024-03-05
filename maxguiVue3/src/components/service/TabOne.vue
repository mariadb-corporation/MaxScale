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
import { MXS_OBJ_TYPES } from '@/constants'
import RelationshipTable from '@/components/details/RelationshipTable.vue'

const props = defineProps({
  obj_data: { type: Object, required: true },
  routingTargetItems: { type: Array, required: true },
  filterItems: { type: Array, required: true },
  listenerItems: { type: Array, required: true },
  fetch: { type: Function, required: true },
  patchParams: { type: Function, required: true },
  handlePatchRelationship: { type: Function, required: true },
  handlePatchRelationships: { type: Function, required: true },
})

const store = useStore()
const fetchObjData = useFetchObjData()

const module_parameters = computed(() => store.state.module_parameters)

async function updateParams(data) {
  await props.patchParams({ id: props.obj_data.id, data, callback: props.fetch })
}

function onClickAddListener() {
  store.commit('SET_FORM_TYPE', MXS_OBJ_TYPES.LISTENERS)
}
</script>

<template>
  <VRow>
    <VCol cols="8">
      <ParametersTable
        :data="obj_data.attributes.parameters"
        :paramsInfo="module_parameters"
        :confirmEdit="updateParams"
        :mxsObjType="MXS_OBJ_TYPES.SERVICES"
      />
    </VCol>
    <VCol cols="4">
      <VRow>
        <VCol cols="12">
          <RelationshipTable
            :objId="obj_data.id"
            type="routingTargets"
            addable
            removable
            :data="routingTargetItems"
            :getRelationshipData="fetchObjData"
            @confirm-update="handlePatchRelationship"
            @confirm-update-relationships="handlePatchRelationships"
          />
        </VCol>
        <VCol cols="12">
          <RelationshipTable
            :type="MXS_OBJ_TYPES.FILTERS"
            addable
            removable
            :data="filterItems"
            :getRelationshipData="fetchObjData"
            @confirm-update="handlePatchRelationship"
          />
        </VCol>
        <VCol cols="12">
          <RelationshipTable
            :type="MXS_OBJ_TYPES.LISTENERS"
            addable
            :data="listenerItems"
            :getRelationshipData="fetchObjData"
            @confirm-update="handlePatchRelationship"
            @click-add-listener="onClickAddListener"
          />
        </VCol>
      </VRow>
    </VCol>
  </VRow>
</template>

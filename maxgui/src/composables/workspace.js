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
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import queryConnService from '@wsServices/queryConnService'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'

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

function useCompletionItems({ queryEditorId, queryTabId }) {
  const store = useStore()
  const queryEditorTmp = computed(() => QueryEditorTmp.find(queryEditorId) || {})
  const queryTabConn = computed(() => queryConnService.findQueryTabConn(queryTabId))
  const queryTabTmp = computed(() => QueryTabTmp.find(queryTabId) || {})
  const activeSchema = computed(() => typy(queryTabConn.value, 'active_db').safeString)
  const snippetCompletionItems = computed(
    () => store.getters['prefAndStorage/snippetCompletionItems']
  )
  const identifier_auto_completion = computed(
    () => store.state.prefAndStorage.identifier_auto_completion
  )
  const schemaTree = computed(() => {
    const tree = typy(queryEditorTmp.value, 'db_tree').safeArray
    if (identifier_auto_completion.value && activeSchema.value)
      return tree.filter((n) => n.qualified_name !== activeSchema.value)
    return tree
  })
  const schemaTreeCompletionItems = computed(() =>
    schemaNodeHelper.genNodeCompletionItems(schemaTree.value)
  )
  const activeSchemaIdentifierCompletionItems = computed(
    () => typy(queryTabTmp.value, 'schema_identifier_names_completion_items').safeArray
  )
  const completionItems = computed(() => [
    ...schemaTreeCompletionItems.value,
    ...activeSchemaIdentifierCompletionItems.value,
    ...snippetCompletionItems.value,
  ])
  return completionItems
}

export default { useCommonResSetAttrs, useCompletionItems }

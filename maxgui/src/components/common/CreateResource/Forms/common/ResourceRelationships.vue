<template>
    <collapse
        wrapperClass="mt-4"
        titleWrapperClass="mx-n9"
        :toggleOnClick="() => (showContent = !showContent)"
        :isContentVisible="showContent"
        :title="`${$tc(relationshipsType, multiple ? 2 : 1)}`"
    >
        <template v-slot:content>
            <select-dropdown
                :entityName="relationshipsType"
                :items="items"
                :multiple="multiple"
                :required="required"
                @get-selected-items="selectedItems = $event"
            />
        </template>
    </collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/* 
This component takes items array props to render v-select component for selecting relationship data. 
Eg : items=[{id:'row_server_1', type:'servers'}]
relationshipsType props is to defined to render correct text, display what relationship type is being target
When getSelectedItems is called by parent component, it returns selectedItems
*/
export default {
    name: 'resource-relationships',

    props: {
        relationshipsType: { type: String, required: true },
        items: { type: Array, required: true },
        multiple: { type: Boolean, default: true },
        required: { type: Boolean, default: false },
    },

    data: function() {
        return {
            showContent: true,
            selectedItems: [],
        }
    },
    methods: {
        getSelectedItems() {
            return this.selectedItems
        },
    },
}
</script>

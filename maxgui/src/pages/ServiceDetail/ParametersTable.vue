<template>
    <details-parameters-collapse
        :resourceId="current_service.id"
        :parameters="current_service.attributes.parameters"
        :updateResourceParameters="updateServiceParameters"
        :onEditSucceeded="onEditSucceeded"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
export default {
    name: 'parameters-table',

    props: {
        onEditSucceeded: { type: Function, required: true },
    },

    computed: {
        ...mapState({
            current_service: state => state.service.current_service,
        }),
    },

    async created() {
        const { router: routerId } = this.current_service.attributes

        await this.fetchModuleParameters(routerId)
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            updateServiceParameters: 'service/updateServiceParameters',
        }),
    },
}
</script>

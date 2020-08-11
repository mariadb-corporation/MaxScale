<template>
    <details-parameters-collapse
        :searchKeyword="search_keyword"
        :resourceId="currentServer.id"
        :parameters="currentServer.attributes.parameters"
        :moduleParameters="processedModuleParameters"
        usePortOrSocket
        :updateResourceParameters="updateServerParameters"
        :onEditSucceeded="onEditSucceeded"
        :loading="loadingModuleParams ? true : loading"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapActions, mapState } from 'vuex'
export default {
    name: 'parameters-table',

    props: {
        onEditSucceeded: { type: Function, required: true },
        loading: { type: Boolean, required: true },
    },

    data() {
        return {
            // parameters
            processedModuleParameters: [],
            loadingModuleParams: true,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            module_parameters: 'module_parameters',
        }),
        ...mapGetters({
            currentServer: 'server/currentServer',
        }),
    },

    async created() {
        await this.fetchModuleParameters('servers')
        this.loadingModuleParams = true
        await this.processModuleParameters()
    },
    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            updateServerParameters: 'server/updateServerParameters',
        }),
        async processModuleParameters() {
            if (this.module_parameters.length) {
                this.processedModuleParameters = this.module_parameters
                const self = this
                await this.$help.delay(150).then(() => (self.loadingModuleParams = false))
            }
        },
    },
}
</script>

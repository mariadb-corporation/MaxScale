<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="module"
            :modules="resourceModules"
            :requiredParams="['filebase']"
        />
    </div>
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
import ModuleParameters from './common/ModuleParameters'

export default {
    name: 'filter-form-input',
    components: {
        ModuleParameters,
    },
    props: {
        resourceModules: { type: Array, required: true },
    },

    methods: {
        getValues() {
            const { moduleId, parameters } = this.$refs.moduleInputs.getModuleInputValues()
            return {
                moduleId: moduleId,
                parameters: parameters,
            }
        },
    },
}
</script>

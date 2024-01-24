<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="module"
            :objType="MXS_OBJ_TYPES.FILTERS"
            v-bind="moduleParamsProps"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from '@share/components/common/ObjectForms/ModuleParameters'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'filter-form-input',
    components: { ModuleParameters },
    props: {
        moduleParamsProps: { type: Object, required: true },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
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

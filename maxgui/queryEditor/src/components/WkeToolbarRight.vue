<template>
    <div ref="wrapper" class="d-flex align-center right-buttons fill-height">
        <conn-man-ctr class="mx-2" />
        <query-cnf-gear-btn />
        <min-max-btn-ctr />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

import ConnManCtr from './ConnManCtr.vue'
import QueryCnfGearBtn from './QueryCnfGearBtn.vue'
import MinMaxBtnCtr from './MinMaxBtnCtr.vue'

export default {
    name: 'wke-toolbar-right',
    components: {
        ConnManCtr,
        QueryCnfGearBtn,
        MinMaxBtnCtr,
    },
    mounted() {
        this.$nextTick(() => this.$emit('get-total-width', this.$refs.wrapper.clientWidth))
    },
}
</script>

<template>
    <portal to="page-header--right">
        <div
            class="d-flex align-center fill-height"
            :style="{ borderBottom: 'thin solid #e7eef1' }"
        >
            <refresh-rate :key="$route.name" v-model="refreshRate" v-on="$listeners" />
            <create-mxs-obj v-if="showCreateNew" class="ml-2 d-inline-block" />
        </div>
    </portal>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
@on-count-done: e: Event. The event is emitted from the `refresh-rate` component.
*/
import refreshRate from '@src/mixins/refreshRate'
export default {
    name: 'page-header-right',
    mixins: [refreshRate],
    props: {
        showCreateNew: { type: Boolean, default: false },
    },
}
</script>

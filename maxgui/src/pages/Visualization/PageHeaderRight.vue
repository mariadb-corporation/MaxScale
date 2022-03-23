<template>
    <portal to="page-header--right">
        <div
            class="d-flex align-center fill-height"
            :style="{ borderBottom: 'thin solid #e7eef1' }"
        >
            <refresh-rate :key="$route.name" :defRefreshRate="60" v-on="$listeners" />
        </div>
    </portal>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
@on-count-done: e: Event. The event is emitted from the `refresh-rate` component.
*/
export default {
    name: 'page-header-right',
}
</script>

<template>
    <div>
        <portal to="page-header">
            <div class="d-flex align-center">
                <div class="d-inline-flex align-center">
                    <h4
                        style="line-height: normal;"
                        class="ml-1 mb-0 color text-navigation display-1 text-capitalize"
                    >
                        {{ $route.name }}
                    </h4>
                </div>
            </div>
        </portal>
        <portal to="page-search">
            <global-search />
        </portal>
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

export default {
    name: 'page-header',
}
</script>
